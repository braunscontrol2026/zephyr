/*
 * Copyright (c) 2020 PHYTEC Messtechnik GmbH
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * This file is based on mbm_core.c from uC/Modbus Stack.
 *
 *                                uC/Modbus
 *                         The Embedded Modbus Stack
 *
 *      Copyright 2003-2020 Silicon Laboratories Inc. www.silabs.com
 *
 *                   SPDX-License-Identifier: APACHE-2.0
 *
 * This software is subject to an open source license and is distributed by
 *  Silicon Laboratories Inc. pursuant to the terms of the Apache License,
 *      Version 2.0 available at www.apache.org/licenses/LICENSE-2.0.
 */

#include <string.h>
#include <zephyr/sys/byteorder.h>
#include "bcbus_internal.h"
#include "syscalls/kernel.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(bcbus_c, CONFIG_BCBUS_LOG_LEVEL);

int bcbus_do_client_handshake(const int iface, const uint8_t unit_id, struct bcbus_tele *buf)
{
	struct bcbus_context *ctx = bcbus_get_context(iface);
	int err;

	if (ctx == NULL) {
		return -ENODEV;
	}

	k_mutex_lock(&ctx->iface_lock, K_FOREVER);

	ctx->tx_adu.unit_id = unit_id;

	err = bcbus_check_unit_present(ctx);
	if (err != 0) {
		goto bcbus_do_client_handshake_ex;
	}

	if (buf->txbuf[1] + 3 > CONFIG_BCBUS_BUFFER_SIZE) {
		err = -EFBIG;
		goto bcbus_do_client_handshake_ex;
	}

	memcpy(ctx->tx_adu.data, &buf->txbuf[2], buf->txbuf[1]);
	ctx->tx_adu.length = buf->txbuf[1];

	err = bcbus_tx_wait_rx_adu(ctx);
	if (err < 0) {
		goto bcbus_do_client_handshake_ex;
	}

	buf->rxbuf[0] = ctx->tx_adu.unit_id;
	buf->rxbuf[1] = ctx->rx_adu.length;
	memcpy(&buf->rxbuf[2], ctx->rx_adu.data, ctx->rx_adu.length);

bcbus_do_client_handshake_ex:
	k_mutex_unlock(&ctx->iface_lock);
	return err;
}

int bcbus_tty_client_new_connect(const int iface)
{
	struct bcbus_context *ctx = bcbus_get_context(iface);

	ring_buf_init(&ctx->tty_rbuf, sizeof(ctx->_ring_buffer_data_tty_rbuf),
		      ctx->_ring_buffer_data_tty_rbuf);
	ring_buf_init(&ctx->tty_sbuf, sizeof(ctx->_ring_buffer_data_tty_sbuf),
		      ctx->_ring_buffer_data_tty_sbuf);
	return 0;
}

int bcbus_tty_client_poll(const int iface)
{
	uint8_t nrec, *p1;
	int nsent, rc;
	struct bcbus_tele _bustele;
	struct bcbus_context *ctx = bcbus_get_context(iface);

	nsent = BUS_MAX_DATA - 4;
	nsent = MIN(nsent, ring_buf_size_get(&ctx->tty_sbuf));
	nsent = MIN(nsent, ctx->slavebuffree);
	nsent = ring_buf_get_claim(&ctx->tty_sbuf, &p1, nsent);
	_bustele.txbuf[1] = (uint8_t)nsent + 3;
	_bustele.txbuf[2] = TTY_CALL;
	_bustele.txbuf[3] = ctx->tty_id++;
	_bustele.txbuf[4] = (uint8_t)nsent;
	memcpy(&_bustele.txbuf[5], p1, nsent);

	rc = bcbus_do_client_handshake(iface, ctx->unit_id, &_bustele);

	if (rc == 0) {
		if (_bustele.rxbuf[1] == 0) {
			/* NAK, not synchron, start new sync */
			ctx->tty_id = 0;
		}
		while (_bustele.rxbuf[1] == 0) {
			/* try synchronisation  */
			_bustele.txbuf[3] = ctx->tty_id++;
			rc = bcbus_do_client_handshake(iface, ctx->unit_id, &_bustele);
			if (rc != 0 || ctx->tty_id == 0) {
				break;
			}
		}
		rc = nsent;
		ctx->slavebuffree = _bustele.rxbuf[2];
		nrec = _bustele.rxbuf[3];
		p1 = &_bustele.rxbuf[4];
		while (nrec > 0) {
			nrec -= ring_buf_put(&ctx->tty_rbuf, p1, nrec);
			if (nrec > 0) {
				p1 += nrec;
				k_sleep(K_MSEC(10));
			}
		}
	}

	ring_buf_get_finish(&ctx->tty_sbuf, MAX(nsent, 0));

	return rc;
}

uint32_t bcbus_tty_client_put(const int iface, const uint8_t unit_id, uint8_t *data, uint32_t size)
{
	struct bcbus_context *ctx = bcbus_get_context(iface);

	if (ctx == NULL) {
		return -ENODEV;
	}

	ctx->unit_id = unit_id;

	return ring_buf_put(&ctx->tty_sbuf, data, size);
}

uint32_t bcbus_tty_client_get(const int iface, const uint8_t unit_id, uint8_t *data, uint32_t size)
{
	uint32_t rc;
	struct bcbus_context *ctx = bcbus_get_context(iface);

	if (ctx == NULL) {
		return -ENODEV;
	}

	ctx->unit_id = unit_id;

	rc = bcbus_tty_client_poll(iface);

	if (rc >= 0) {
		rc = ring_buf_get(&ctx->tty_rbuf, data, size);
	}

	return rc;
}
