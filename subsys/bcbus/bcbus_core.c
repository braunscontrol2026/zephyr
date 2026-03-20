/*
 * Copyright (c) 2020 PHYTEC Messtechnik GmbH
 * Copyright (c) 2021 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(bcbus_core_c, CONFIG_BCBUS_LOG_LEVEL);

#include <zephyr/kernel.h>
#include <string.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/bcbus/bcbus_unit.h>
#include "bcbus_internal.h"

#define DT_DRV_COMPAT zephyr_bcbus_serial

#define MB_RTU_DEFINE_GPIO_CFG(inst, prop)                                                         \
	static struct gpio_dt_spec prop##_cfg_##inst = {                                           \
		.port = DEVICE_DT_GET(DT_INST_PHANDLE(inst, prop)),                                \
		.pin = DT_INST_GPIO_PIN(inst, prop),                                               \
		.dt_flags = DT_INST_GPIO_FLAGS(inst, prop),                                        \
	};

#define MB_RTU_DEFINE_GPIO_CFGS(inst)                                                              \
	COND_CODE_1(DT_INST_NODE_HAS_PROP(inst, de_gpios),		\
		    (MB_RTU_DEFINE_GPIO_CFG(inst, de_gpios)), ())                                      \
	COND_CODE_1(DT_INST_NODE_HAS_PROP(inst, re_gpios),		\
		    (MB_RTU_DEFINE_GPIO_CFG(inst, re_gpios)), ())

DT_INST_FOREACH_STATUS_OKAY(MB_RTU_DEFINE_GPIO_CFGS)

#define MB_RTU_ASSIGN_GPIO_CFG(inst, prop)                                                         \
	COND_CODE_1(DT_INST_NODE_HAS_PROP(inst, prop),		\
		    (&prop##_cfg_##inst), (NULL))

#define BCBUS_DT_GET_SERIAL_DEV(inst)                                                              \
	{                                                                                          \
		.dev = DEVICE_DT_GET(DT_INST_PARENT(inst)),                                        \
		.de = MB_RTU_ASSIGN_GPIO_CFG(inst, de_gpios),                                      \
		.re = MB_RTU_ASSIGN_GPIO_CFG(inst, re_gpios),                                      \
	},

#ifdef CONFIG_BCBUS_SERIAL
static struct bcbus_serial_config bcbus_serial_cfg[] = {
	DT_INST_FOREACH_STATUS_OKAY(BCBUS_DT_GET_SERIAL_DEV)};
#endif

#define BCBUS_DT_GET_DEV(inst)                                                                     \
	{                                                                                          \
		.iface_name = DEVICE_DT_NAME(DT_DRV_INST(inst)),                                   \
		.cfg = &bcbus_serial_cfg[inst],                                                    \
	},

#define DEFINE_BCBUS_RAW_ADU(x, _)                                                                 \
	{                                                                                          \
		.iface_name = "RAW_" #x,                                                           \
		.rawcb.raw_tx_cb = NULL,                                                           \
	}

static struct bcbus_context mb_ctx_tbl[] = {
	DT_INST_FOREACH_STATUS_OKAY(BCBUS_DT_GET_DEV)
#ifdef CONFIG_BCBUS_RAW_ADU
		LISTIFY(CONFIG_BCBUS_NUMOF_RAW_ADU, DEFINE_BCBUS_RAW_ADU, (,), _)
#endif
};

static void bcbus_rx_handler(struct k_work *item)
{
	struct bcbus_context *ctx;

	ctx = CONTAINER_OF(item, struct bcbus_context, server_work);

	if (IS_ENABLED(CONFIG_BCBUS_SERIAL)) {
		bcbus_serial_rx_disable(ctx);
		ctx->rx_adu_err = bcbus_serial_rx_adu(ctx);
	}

	if (ctx->client == true) {
		k_sem_give(&ctx->client_wait_sem);
	} else if (IS_ENABLED(CONFIG_BCBUS_SERVER)) {
		bool respond = bcbus_server_handler(ctx);

		if (respond) {
			bcbus_tx_adu(ctx);
		} else {
			LOG_DBG("%s: Server has dropped frame", ctx->iface_name);
		}

		if (IS_ENABLED(CONFIG_BCBUS_SERIAL) && respond == false) {
			bcbus_serial_rx_enable(ctx);
		}
	}
}

void bcbus_tx_adu(struct bcbus_context *ctx)
{
	if (IS_ENABLED(CONFIG_BCBUS_SERIAL) && bcbus_serial_tx_adu(ctx)) {
		LOG_ERR("%s: Unsupported BCBUS serial mode", ctx->iface_name);
	}
}

void bcbus_tx_ba(struct bcbus_context *ctx)
{
	if (IS_ENABLED(CONFIG_BCBUS_SERIAL) && bcbus_serial_tx_ba(ctx)) {
		LOG_ERR("%s: Unsupported BCBUS serial mode", ctx->iface_name);
	}
}

int bcbus_check_unit_present(struct bcbus_context *ctx)
{
	k_sem_reset(&ctx->client_wait_sem);

	bcbus_tx_ba(ctx);

	if (k_sem_take(&ctx->client_wait_sem, K_USEC(ctx->baecho_to)) != 0) {
		LOG_WRN("%s-%d Client wait-for-BA-echo timeout", ctx->iface_name, ctx->unit_id);
		return -ETIMEDOUT;
	}

	return ctx->rx_adu_err;
}

int bcbus_tx_wait_rx_adu(struct bcbus_context *ctx)
{
	k_sem_reset(&ctx->client_wait_sem);

	bcbus_tx_adu(ctx);

	if (k_sem_take(&ctx->client_wait_sem, K_USEC(ctx->rxwait_to)) != 0) {
		LOG_WRN("%s-%d Client wait-for-RX timeout", ctx->iface_name, ctx->tx_adu.unit_id);
		return -ETIMEDOUT;
	}

	return ctx->rx_adu_err;
}

int bcbus_find_ba_iface(const uint8_t ba)
{
	struct bcbus_context *ctx;
	struct bcbus_obj *src;
	struct busunit *p;
	int n;

	for (n = 0; n < ARRAY_SIZE(mb_ctx_tbl); n++) {
		ctx = &mb_ctx_tbl[n];
		src = ctx->bus_item_prop_list;
		if (src == NULL) {
			continue;
		}
		for (p = bcbus_item_head(src); p != NULL; p = bcbus_item_next(p)) {
			if (p->ba == ba) {
				return n;
			}
		}
	}
	return -1;
}

struct bcbus_context *bcbus_get_context(const uint8_t iface)
{
	struct bcbus_context *ctx;

	if (iface >= ARRAY_SIZE(mb_ctx_tbl)) {
		LOG_ERR("Interface idx %u not available", iface);
		return NULL;
	}

	ctx = &mb_ctx_tbl[iface];

	if (!atomic_test_bit(&ctx->state, BCBUS_STATE_CONFIGURED)) {
		LOG_ERR("Interface idx %u not configured", iface);
		return NULL;
	}

	return ctx;
}

int bcbus_iface_get_by_ctx(const struct bcbus_context *ctx)
{
	for (int i = 0; i < ARRAY_SIZE(mb_ctx_tbl); i++) {
		if (&mb_ctx_tbl[i] == ctx) {
			return i;
		}
	}

	return -ENODEV;
}

int bcbus_iface_get_by_name(const char *iface_name)
{
	for (int i = 0; i < ARRAY_SIZE(mb_ctx_tbl); i++) {
		if (strcmp(iface_name, mb_ctx_tbl[i].iface_name) == 0) {
			return i;
		}
	}

	return -ENODEV;
}

static struct bcbus_context *bcbus_init_iface(const uint8_t iface)
{
	struct bcbus_context *ctx;

	if (iface >= ARRAY_SIZE(mb_ctx_tbl)) {
		LOG_ERR("Interface idx %u not available", iface);
		return NULL;
	}

	ctx = &mb_ctx_tbl[iface];

	if (atomic_test_and_set_bit(&ctx->state, BCBUS_STATE_CONFIGURED)) {
		LOG_ERR("Interface idx %u already used", iface);
		return NULL;
	}

	if (ctx->bus_item_prop_list == NULL) {
		ctx->bus_item_prop_list = new_empty_bcbus_obj(ctx->iface_name);
	}

	k_mutex_init(&ctx->iface_lock);
	k_sem_init(&ctx->client_wait_sem, 0, 1);
	k_work_init(&ctx->server_work, bcbus_rx_handler);

	return ctx;
}

int bcbus_init_server(const int iface, struct bcbus_iface_param param)
{
	struct bcbus_context *ctx = NULL;
	int rc = 0;

	if (!IS_ENABLED(CONFIG_BCBUS_SERVER)) {
		LOG_ERR("BC-Bus server support is not enabled");
		rc = -ENOTSUP;
		goto init_server_error;
	}

	if (param.server.user_cb == NULL) {
		LOG_ERR("User callbacks should be available");
		rc = -EINVAL;
		goto init_server_error;
	}

	ctx = bcbus_init_iface(iface);
	if (ctx == NULL) {
		rc = -EINVAL;
		goto init_server_error;
	}

	ctx->client = false;

	if (IS_ENABLED(CONFIG_BCBUS_SERIAL) && bcbus_serial_init(ctx, param) != 0) {
		LOG_ERR("Failed to init %s", ctx->iface_name);
		rc = -EINVAL;
		goto init_server_error;
	}

	ctx->unit_id = param.server.unit_id;
	ctx->mbs_user_cb = param.server.user_cb;
	if (IS_ENABLED(CONFIG_BCBUS_FC08_DIAGNOSTIC)) {
		bcbus_reset_stats(ctx);
	}

	LOG_DBG("BC-Bus interface %s initialized", ctx->iface_name);

	return 0;

init_server_error:
	if (ctx != NULL) {
		atomic_clear_bit(&ctx->state, BCBUS_STATE_CONFIGURED);

		if (ctx->bus_item_prop_list != NULL) {
			delete_bcbus_obj(ctx->bus_item_prop_list);
			ctx->bus_item_prop_list = NULL;
		}
	}

	return rc;
}

int bcbus_register_user_fc(const int iface, struct bcbus_custom_fc *custom_fc)
{
	struct bcbus_context *ctx = bcbus_get_context(iface);

	if (!custom_fc) {
		LOG_ERR("Provided function code handler was NULL");
		return -EINVAL;
	}

	if (custom_fc->fc & BIT(7)) {
		LOG_ERR("Function codes must have MSB of 0");
		return -EINVAL;
	}

	custom_fc->excep_code = BCBUS_EXC_NONE;

	LOG_DBG("Registered new custom function code %d", custom_fc->fc);
	sys_slist_append(&ctx->user_defined_cbs, &custom_fc->node);

	return 0;
}

struct bcbus_obj *bcbus_get_copy_poll_list(const int iface)
{
	struct bcbus_context *ctx = bcbus_get_context(iface);
	struct bcbus_obj *dest, *src;
	struct busunit *p;

	src = ctx->bus_item_prop_list;
	dest = new_empty_bcbus_obj(ctx->iface_name);
	if (dest != NULL) {
		for (p = bcbus_item_head(src); p != NULL; p = bcbus_item_next(p)) {
			bcbus_cpy_itemdata(bcbus_add_item(dest, p->ba), p);
		}
	}

	return dest;
}

int bcbus_replace_poll_list(const int iface, struct bcbus_obj *newlist)
{
	struct bcbus_context *ctx;

	ctx = bcbus_get_context(iface);
	if (ctx == NULL) {
		LOG_ERR("Interface %u not initialized", iface);
		return -EINVAL;
	}

	if (ctx->bus_item_prop_list != NULL) {
		delete_bcbus_obj(ctx->bus_item_prop_list);
	}
	ctx->bus_item_prop_list = newlist;

	return 0;
}

int bcbus_init_client(const int iface, struct bcbus_iface_param param)
{
	struct bcbus_context *ctx = NULL;
	int rc = 0;

	if (!IS_ENABLED(CONFIG_BCBUS_CLIENT)) {
		LOG_ERR("BC-Bus client support is not enabled");
		rc = -ENOTSUP;
		goto init_client_error;
	}

	ctx = bcbus_init_iface(iface);
	if (ctx == NULL) {
		rc = -EINVAL;
		goto init_client_error;
	}

	ctx->client = true;

	if (IS_ENABLED(CONFIG_BCBUS_SERIAL) && bcbus_serial_init(ctx, param) != 0) {
		LOG_ERR("Failed to init BCBUS over serial line");
		rc = -EINVAL;
		goto init_client_error;
	}

	ctx->unit_id = 0;
	ctx->mbs_user_cb = NULL;
	ctx->rxwait_to = param.rx_timeout;
	ctx->baecho_to = param.ba_echo_timeout;

	return 0;

init_client_error:
	if (ctx != NULL) {
		atomic_clear_bit(&ctx->state, BCBUS_STATE_CONFIGURED);

		if (ctx->bus_item_prop_list != NULL) {
			delete_bcbus_obj(ctx->bus_item_prop_list);
			ctx->bus_item_prop_list = NULL;
		}
	}

	return rc;
}

int bcbus_disable(const uint8_t iface)
{
	struct bcbus_context *ctx;
	struct k_work_sync work_sync;
	const char *name;

	ctx = bcbus_get_context(iface);
	if (ctx == NULL) {
		LOG_ERR("Interface %u not initialized", iface);
		return -EINVAL;
	}

	name = ctx->iface_name;

	if (IS_ENABLED(CONFIG_BCBUS_SERIAL)) {
		bcbus_serial_disable(ctx);
	}

	k_work_cancel_sync(&ctx->server_work, &work_sync);
	ctx->rxwait_to = 0;
	ctx->baecho_to = 0;
	ctx->unit_id = 0;
	ctx->mbs_user_cb = NULL;
	atomic_clear_bit(&ctx->state, BCBUS_STATE_CONFIGURED);
	if (ctx->bus_item_prop_list != NULL) {
		delete_bcbus_obj(ctx->bus_item_prop_list);
		ctx->bus_item_prop_list = NULL;
	}

	LOG_INF("%s interface %u disabled", name, iface);

	return 0;
}
