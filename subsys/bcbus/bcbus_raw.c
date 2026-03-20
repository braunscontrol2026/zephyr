/*
 * Copyright (c) 2021 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(bcbus_raw, CONFIG_BCBUS_LOG_LEVEL);

#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>
#include "bcbus_internal.h"

#define BCBUS_ADU_LENGTH_DEVIATION	2
#define BCBUS_RAW_MIN_MSG_SIZE		(BCBUS_RTU_MIN_MSG_SIZE - 2)
#define BCBUS_RAW_BUFFER_SIZE		(CONFIG_BCBUS_BUFFER_SIZE - 2)

int bcbus_raw_rx_adu(struct bcbus_context *ctx)
{
	if (ctx->rx_adu.length < BCBUS_RAW_MIN_MSG_SIZE ||
	    ctx->rx_adu.length > BCBUS_RAW_BUFFER_SIZE) {
		LOG_WRN("Frame length error");
		return -EMSGSIZE;
	}

	if (ctx->rx_adu.proto_id != BCBUS_ADU_PROTO_ID) {
		LOG_ERR("BCBUS protocol not supported");
		return -ENOTSUP;
	}

	return 0;
}


void bcbus_raw_put_header(const struct bcbus_adu *adu, uint8_t *header)
{
	uint16_t length = MIN(adu->length, CONFIG_BCBUS_BUFFER_SIZE);

	sys_put_be16(adu->trans_id, &header[0]);
	sys_put_be16(adu->proto_id, &header[2]);
	sys_put_be16(length + BCBUS_ADU_LENGTH_DEVIATION, &header[4]);
	header[6] = adu->unit_id;
	header[7] = adu->fc;
}

void bcbus_raw_get_header(struct bcbus_adu *adu, const uint8_t *header)
{
	adu->trans_id = sys_get_be16(&header[0]);
	adu->proto_id = sys_get_be16(&header[2]);
	adu->length = MIN(sys_get_be16(&header[4]), CONFIG_BCBUS_BUFFER_SIZE);
	adu->unit_id = header[6];
	adu->fc = header[7];

	if (adu->length >= BCBUS_ADU_LENGTH_DEVIATION) {
		adu->length -= BCBUS_ADU_LENGTH_DEVIATION;
	}
}

static void bcbus_set_exception(struct bcbus_adu *adu,
				 const uint8_t excep_code)
{
	const uint8_t excep_bit = BIT(7);

	adu->fc |= excep_bit;
	adu->data[0] = excep_code;
	adu->length = 1;
}

void bcbus_raw_set_server_failure(struct bcbus_adu *adu)
{
	const uint8_t excep_bit = BIT(7);

	adu->fc |= excep_bit;
	adu->data[0] = BCBUS_EXC_SERVER_DEVICE_FAILURE;
	adu->length = 1;
}

int bcbus_raw_backend_txn(const int iface, struct bcbus_adu *adu)
{
	struct bcbus_context *ctx;
	int err;

	ctx = bcbus_get_context(iface);
	if (ctx == NULL) {
		LOG_ERR("Interface %d not available", iface);
		bcbus_set_exception(adu, BCBUS_EXC_GW_PATH_UNAVAILABLE);
		return -ENODEV;
	}

	LOG_DBG("Use backend interface %d", iface);
	memcpy(&ctx->tx_adu, adu, sizeof(struct bcbus_adu));
	err = bcbus_tx_wait_rx_adu(ctx);

	if (err == 0) {
		/*
		 * Serial line does not use transaction and protocol IDs.
		 * Temporarily store transaction and protocol IDs, and write it
		 * back if the transfer was successful.
		 */
		uint16_t trans_id = adu->trans_id;
		uint16_t proto_id = adu->proto_id;

		memcpy(adu, &ctx->rx_adu, sizeof(struct bcbus_adu));
		adu->trans_id = trans_id;
		adu->proto_id = proto_id;
	} else {
		bcbus_set_exception(adu, BCBUS_EXC_GW_TARGET_FAILED_TO_RESP);
	}

	return err;
}

int bcbus_raw_init(struct bcbus_context *ctx,
		    struct bcbus_iface_param param)
{

	ctx->rawcb.raw_tx_cb = param.rawcb.raw_tx_cb;
	ctx->rawcb.user_data = param.rawcb.user_data;

	return 0;
}

void bcbus_raw_disable(struct bcbus_context *ctx)
{
}
