/*
 * Copyright (c) 2025 Brauns Control GmbH.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "psram.h"
#include "zephyr/devicetree.h"
#include <zephyr/logging/log.h>
#include <stdint.h>
#include <stdlib.h>
LOG_MODULE_REGISTER(polling_c, LOG_LEVEL_DBG);

#include <zephyr/kernel.h>
#include <zephyr/bcbus/bcbus.h>
#include <zephyr/bcbus/bcbus_unit.h>
#include "polling.h"

#define POLL_BUFFER_COUNT 12

K_MEM_SLAB_DEFINE_STATIC(poll_slab, sizeof(struct poll_data), POLL_BUFFER_COUNT, 4);
struct poll_buf_ctx {
	const struct poll_data *addr;
	struct k_mutex ctx_mutex;
};
static struct poll_buf_ctx poll_buf_stat[POLL_BUFFER_COUNT];
K_MUTEX_DEFINE(poll_buf_mutex);

static void poll_buf_lock_wait(const struct poll_data *src)
{
	int n;

	k_mutex_lock(&poll_buf_mutex, K_FOREVER);
	for (n = 0; n < POLL_BUFFER_COUNT; n++) {
		if (poll_buf_stat[n].addr == src) {
			k_mutex_unlock(&poll_buf_mutex);
			k_mutex_lock(&poll_buf_stat[n].ctx_mutex, K_FOREVER);
			return;
		}
	}
	for (n = 0; n < POLL_BUFFER_COUNT; n++) {
		if (poll_buf_stat[n].addr == NULL) {
			poll_buf_stat[n].addr = src;
			k_mutex_unlock(&poll_buf_mutex);
			k_mutex_lock(&poll_buf_stat[n].ctx_mutex, K_FOREVER);
			return;
		}
	}
}

static void poll_buf_unlock(const struct poll_data *src)
{
	int n;

	k_mutex_lock(&poll_buf_mutex, K_FOREVER);
	for (n = 0; n < POLL_BUFFER_COUNT; n++) {
		if (poll_buf_stat[n].addr == src) {
			poll_buf_stat[n].addr = NULL;
			k_mutex_unlock(&poll_buf_stat[n].ctx_mutex);
			break;
		}
	}
	k_mutex_unlock(&poll_buf_mutex);
}

static void poll_buf_init(void)
{
	int n;

	for (n = 0; n < POLL_BUFFER_COUNT; n++) {
		poll_buf_stat[n].addr = NULL;
		k_mutex_init(&poll_buf_stat[n].ctx_mutex);
	}
}

struct poll_data *claim_poll_data_buf(const struct poll_data *src)
{
	struct poll_data *p = NULL;
	poll_buf_lock_wait(src);
	if (k_mem_slab_alloc(&poll_slab, (void *)&p, K_MSEC(100)) == 0) {
		p = psmemcpy(p, src, sizeof(struct poll_data));
	} else {
		LOG_ERR("no free POLL_BUFFERS");
	}
	return p;
}

void release_poll_data_buf(struct poll_data *dest, const struct poll_data *src)
{
	psmemcpy(dest, src, sizeof(struct poll_data));
	k_mem_slab_free(&poll_slab, (void *)src);
	poll_buf_unlock(dest);
}

static struct poll_data *fetch_poll_data(uint8_t adr)
{
	struct poll_data *p = (void *)PSRAM_POLL_DATA_ADR(adr);
	return claim_poll_data_buf(p);
}

static inline void release_poll_data(uint8_t adr, struct poll_data *p)
{
	struct poll_data *dest = (void *)PSRAM_POLL_DATA_ADR(adr);
	release_poll_data_buf(dest, p);
}

struct polling_th {
	/* UART device */
	const struct device *dev;
	enum poll_state state;
	int iface;
	struct bcbus_obj *bus;
};

char const *const status_str[] = {"idle", "starting", "running", "stopping", "not found"};

#define DT_DRV_COMPAT zephyr_bcbus_serial

#define BCBUS_DT_GET_SERIAL_DEV(inst)                                                              \
	{                                                                                          \
		.dev = DEVICE_DT_GET(DT_INST_PARENT(inst)),                                        \
	},

static struct polling_th polling_ths[] = {
	DT_INST_FOREACH_STATUS_OKAY(BCBUS_DT_GET_SERIAL_DEV){.dev = NULL},
};

#define BUS_COUNT (sizeof(polling_ths) / sizeof(struct polling_th))

static struct busunit *unit_tab[256];

struct busunit *get_polled_busunit(uint8_t ba)
{
	return unit_tab[ba];
}

static bool is_not_empty(uint8_t buf[BCBUS_MAPPING])
{
	int n;
	for (n = 0; n < BCBUS_MAPPING; n++) {
		if (buf[n] != 0) {
			return true;
		}
	}
	return false;
}

static void put_setpoint_block(uint8_t adr, uint8_t *data)
{
	struct poll_data *dp;
	struct busunit *p = unit_tab[adr];
	if (p != NULL && p->data != NULL) {
		dp = p->data;
		psmemcpy(&dp->data[0], data, BCBUS_MAPPING);
	}
}

size_t write_setpoint_chunk(uint16_t offset, uint8_t *data, size_t len)
{
	static uint8_t split_buf[BCBUS_MAPPING];
	static uint16_t split;
	uint16_t adr_block;
	size_t to_write = len;
	size_t written = 0;
	int n;
	if (offset == 0) {
		split = 0;
	}
	adr_block = offset - split;
	while (to_write) {
		n = MIN(BCBUS_MAPPING - split, to_write);
		memcpy(&split_buf[split], &data[written], n);
		written += n;
		split += n;
		to_write -= n;
		if (split == BCBUS_MAPPING) {
			if (is_not_empty(split_buf)) {
				put_setpoint_block(adr_block / BCBUS_MAPPING, split_buf);
			}
			adr_block += BCBUS_MAPPING;
			split = 0;
		}
	}
	return len;
}

static void get_upstream_block(uint8_t adr, uint8_t *dest)
{
	struct poll_data *dp;
	struct busunit *p = unit_tab[adr];
	if (p != NULL && p->data != NULL) {
		dp = p->data;
		psmemcpy(dest, &dp->data[2], BCBUS_MAPPING);
	} else {
		memset(dest, 0, BCBUS_MAPPING);
	}
}

size_t read_upstream_chunk(uint16_t offset, uint8_t *buf, size_t bufsize)
{
	int rest;
	uint8_t *cursor = buf;
	uint16_t adr = offset;
	size_t len, nbytes;
	uint8_t blk[BCBUS_MAPPING];

	for (nbytes = 0; nbytes < bufsize; nbytes += len) {
		rest = adr % BCBUS_MAPPING;
		len = MIN(bufsize - nbytes, BCBUS_MAPPING - rest);
		get_upstream_block(adr / BCBUS_MAPPING, blk);
		memcpy(cursor, blk + rest, len);
		cursor += len;
		adr += len;
	}
	return nbytes;
}

static int get_thd_state_idx(enum poll_state state)
{
	int n = 0;
	while (polling_ths[n].dev != NULL) {
		if (polling_ths[n].state == state) {
			return n;
		}
		n++;
	}
	return -1;
}

static int get_thd_iface_idx(int iface)
{
	int n = 0;
	while (polling_ths[n].dev != NULL) {
		if (polling_ths[n].iface == iface) {
			return n;
		}
		n++;
	}
	return -1;
}

void start_polling(int iface)
{
	int n;

	n = get_thd_iface_idx(iface);

	if (n < 0) {
		n = get_thd_state_idx(WAIT_START);
	}

	if (n >= 0) {
		if (polling_ths[n].state != RUNNING) {
			polling_ths[n].state = START_POLL;
		}
		polling_ths[n].iface = iface;
	}
}

void stop_polling(int iface)
{
	int n;

	n = get_thd_iface_idx(iface);

	if (n >= 0) {
		polling_ths[n].state = STOP_POLL;
	}
}

enum poll_state status_polling(int iface)
{
	int n;

	n = get_thd_iface_idx(iface);

	if (n >= 0) {
		return polling_ths[n].state;
	}
	return NOT_VALID;
}

const char *poll_state_txt(enum poll_state state)
{
	return status_str[state];
}

char *get_poll_stats(char *buf, size_t buf_size, int iface, uint8_t ba)
{
	unsigned int p_stat;
	int n;
	struct busunit *item;

	strncpy(buf, "invalid", buf_size);

	n = get_thd_iface_idx(iface);
	if (n >= 0 && polling_ths[n].bus != NULL) {
		item = bcbus_find_item_ba(polling_ths[n].bus, ba);
		if (item != NULL) {
			p_stat = ((uint64_t)item->poll_cnt - item->err_cnt) * 1000 / item->poll_cnt;
			snprintf(buf, buf_size, "%3u.%01u ok from %u", p_stat / 10, p_stat % 10,
				 item->poll_cnt);
		}
	}
	return buf;
}

static void do_polling(struct polling_th *p)
{
	int rc;
	struct busunit *pbu;
	uint8_t *r_unit;
	struct poll_data *dp;

	struct bcbus_tele bustele_snr = {
		.txbuf = {0, 1, SEND_SER_NUM},
	};

	p->bus = bcbus_get_copy_poll_list(p->iface);
	if (p->bus == NULL) {
		LOG_WRN("no buslist");
		return;
	}

	for (pbu = bcbus_item_head(p->bus); pbu != NULL; pbu = bcbus_item_next(pbu)) {
		unit_tab[pbu->ba] = pbu;
		pbu->data = PSRAM_START_ADR + pbu->ba * sizeof(struct poll_data);
		psmemset(pbu->data, 0, sizeof(struct poll_data));
	}

	while (p->state == RUNNING) {
		for (pbu = bcbus_item_head(p->bus); pbu != NULL; pbu = bcbus_item_next(pbu)) {
			dp = fetch_poll_data(pbu->ba);
			pbu->data = dp;
			if (dp == NULL) {
				break;
			}
			r_unit = dp->data[2];
			if (r_unit[2] == 0) {
				rc = bcbus_do_client_handshake(p->iface, pbu->ba, &bustele_snr);
				if (rc == 0) {
					memcpy(&r_unit[2], &bustele_snr.rxbuf[2],
					       bustele_snr.rxbuf[1]);
				}
			}
			rc = poll_busunit(p->iface, pbu);
			if (rc == 0 && r_unit[1] > 0) {
				r_unit[1] /= 2;
			}
			pbu->data = PSRAM_POLL_DATA_ADR(pbu->ba);
			release_poll_data(pbu->ba, dp);
		}
		k_sleep(K_MSEC(250));
	}

	for (pbu = bcbus_item_head(p->bus); pbu != NULL; pbu = bcbus_item_next(pbu)) {
		unit_tab[pbu->ba] = NULL;
	}

	delete_bcbus_obj(p->bus);
	p->bus = NULL;
}

static void polling_handler(void *p1, void *p2, void *p3)
{
	int n;
	struct polling_th *act;

	while (1) {
		n = get_thd_state_idx(START_POLL);

		if (n < 0) {
			k_sleep(K_MSEC(1000));
		} else {
			act = &polling_ths[n];
			act->state = RUNNING;
			do_polling(act);
			act->state = WAIT_START;
		}
	}
}

void polling_init(void)
{
	poll_buf_init();
}

#define STACKSIZE 1024

K_THREAD_DEFINE(bcbus_poll0, STACKSIZE, polling_handler, NULL, NULL, NULL, 14, 0, 10);
K_THREAD_DEFINE(bcbus_poll1, STACKSIZE, polling_handler, NULL, NULL, NULL, 14, 0, 10);
K_THREAD_DEFINE(bcbus_poll2, STACKSIZE, polling_handler, NULL, NULL, NULL, 14, 0, 10);
K_THREAD_DEFINE(bcbus_poll3, STACKSIZE, polling_handler, NULL, NULL, NULL, 14, 0, 10);
