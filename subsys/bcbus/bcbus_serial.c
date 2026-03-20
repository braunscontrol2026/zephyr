/*
 * Copyright (c) 2020 PHYTEC Messtechnik GmbH
 * Copyright (c) 2021 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * This file is based on mb.c and mb_util.c from uC/Modbus Stack.
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

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(bcbus_serial_c, CONFIG_BCBUS_LOG_LEVEL);

#include <zephyr/kernel.h>
#include <string.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/crc.h>
#include <component/sercom.h>
#include "bcbus_internal.h"

static void bcbus_serial_tx_on(struct bcbus_context *ctx)
{
	struct bcbus_serial_config *cfg = ctx->cfg;

	if (cfg->de != NULL) {
		gpio_pin_set(cfg->de->port, cfg->de->pin, 1);
	}

	uart_irq_tx_enable(cfg->dev);
}

static void bcbus_serial_tx_off(struct bcbus_context *ctx)
{
	struct bcbus_serial_config *cfg = ctx->cfg;

	uart_irq_tx_disable(cfg->dev);
	if (cfg->de != NULL) {
		gpio_pin_set(cfg->de->port, cfg->de->pin, 0);
	}
}

static void bcbus_serial_rx_on(struct bcbus_context *ctx)
{
	struct bcbus_serial_config *cfg = ctx->cfg;
	uint8_t c;

	if (cfg->re != NULL) {
		gpio_pin_set(cfg->re->port, cfg->re->pin, 1);
	}

	while (uart_fifo_read(cfg->dev, &c, 1))
		; /* XXX patch IB 28.08.25 */
	uart_irq_rx_enable(cfg->dev);
}

static void bcbus_serial_rx_off(struct bcbus_context *ctx)
{
	struct bcbus_serial_config *cfg = ctx->cfg;

	uart_irq_rx_disable(cfg->dev);
	if (cfg->re != NULL) {
		gpio_pin_set(cfg->re->port, cfg->re->pin, 0);
	}
}

static inline int reconfigure_baud(struct bcbus_context *ctx, uint32_t baud)
{
	struct bcbus_serial_config *cfg = ctx->cfg;
	struct uart_config uart_cfg;

	uart_config_get(cfg->dev, &uart_cfg);
	uart_cfg.baudrate = baud;

	if (uart_configure(cfg->dev, &uart_cfg) != 0) {
		LOG_ERR("%s: Failed to configure UART", ctx->iface_name);
		return -EINVAL;
	}

	return 0;
}

/* Check valid ECHO. */
static int bcbus_ba_rx(struct bcbus_context *ctx)
{
	struct bcbus_serial_config *cfg = ctx->cfg;

	if (cfg->adjust_ba_time) {
		reconfigure_baud(ctx, 9600);
	}

	ctx->rx_adu.unit_id = cfg->uart_buf[0];

	if (cfg->check_ba_echo) {
		/* Is valid BA */
		if ((ctx->rx_adu.unit_id != ctx->tx_adu.unit_id) || (cfg->uart_bit9 == 0)) {
			LOG_WRN("%s: Frame wrong BA error: %d", ctx->iface_name,
				ctx->tx_adu.unit_id);
			k_sleep(K_MSEC(50));
			return -EFAULT;
		}
	}
	cfg->uart_bit9 = false;
	k_sleep(K_USEC(400));
	return 0;
}

/* Copy Modbus RTU frame and check if the CRC is valid. */
static int bcbus_rtu_rx_adu(struct bcbus_context *ctx)
{
	struct bcbus_serial_config *cfg = ctx->cfg;
	uint16_t calc_crc;
	uint16_t crc_idx;
	uint8_t *data_ptr;

	/* if wait for echo, branch */
	if (cfg->uart_bit9) {
		return bcbus_ba_rx(ctx);
	}

	/* Is the message long enough? */
	if ((cfg->uart_buf_ctr < BCBUS_RTU_MIN_MSG_SIZE) ||
	    (cfg->uart_buf_ctr > CONFIG_BCBUS_BUFFER_SIZE + 4)) {
		LOG_WRN("%s Frame length error ba %d", ctx->iface_name, ctx->unit_id);
		return -EMSGSIZE;
	}

	/* Payload length without node address, length byte, and CRC */
	if (cfg->uart_buf[0] > CONFIG_BCBUS_BUFFER_SIZE - 3) {
		LOG_WRN("%s Payload overflow error ba %d", ctx->iface_name, ctx->unit_id);
		return -EMSGSIZE;
	}

	ctx->rx_adu.length = cfg->uart_buf[0];
	data_ptr = &cfg->uart_buf[1];
	/* CRC index */
	crc_idx = cfg->uart_buf_ctr - sizeof(uint16_t);

	ctx->rx_adu.crc = sys_get_le16(&cfg->uart_buf[crc_idx]);
	/* Calculate CRC over  */
	calc_crc = crc16_ccitt(0x0000, &cfg->uart_buf[0], cfg->uart_buf[0] + 1);

	if (ctx->rx_adu.crc != calc_crc) {
		LOG_WRN("%s Calculated CRC does not match received CRC ba %d", ctx->iface_name,
			ctx->unit_id);
		return -EIO;
	}

	memcpy(ctx->rx_adu.data, data_ptr, ctx->rx_adu.length);

	return 0;
}

static void rtu_tx_adu(struct bcbus_context *ctx) /* XXX IB.hier weiter */
{
	struct bcbus_serial_config *cfg = ctx->cfg;
	uint16_t tx_bytes = 0;
	uint8_t *data_ptr;

	cfg->uart_buf[0] = ctx->tx_adu.unit_id;
	cfg->uart_buf[1] = ctx->tx_adu.length;
	tx_bytes = 1 + ctx->tx_adu.length;
	data_ptr = &cfg->uart_buf[2];

	memcpy(data_ptr, ctx->tx_adu.data, ctx->tx_adu.length);

	ctx->tx_adu.crc = crc16_ccitt(0x0000, &cfg->uart_buf[0], ctx->tx_adu.length + 2);
	sys_put_le16(ctx->tx_adu.crc, &cfg->uart_buf[ctx->tx_adu.length + 2]);
	tx_bytes += 2;

	cfg->uart_buf_ctr = tx_bytes;
	cfg->uart_buf_ptr = &cfg->uart_buf[1];

	LOG_HEXDUMP_DBG(cfg->uart_buf, cfg->uart_buf_ctr, "uart_buf");
	LOG_DBG("Start frame transmission");
	bcbus_serial_rx_off(ctx);
	bcbus_serial_tx_on(ctx);
}

static void rtu_tx_ba(struct bcbus_context *ctx)
{
	struct bcbus_serial_config *cfg = ctx->cfg;

	cfg->uart_buf[0] = ctx->tx_adu.unit_id;

	cfg->uart_buf_ctr = 1;
	cfg->uart_buf_ptr = &ctx->tx_adu.unit_id;
	cfg->uart_bit9 = true;

	LOG_HEXDUMP_DBG(cfg->uart_buf, cfg->uart_buf_ctr, "uart_buf");
	LOG_DBG("Start frame transmission");

	if (cfg->adjust_ba_time) {
		reconfigure_baud(ctx, 10060);
	}
	bcbus_serial_rx_off(ctx);
	bcbus_serial_tx_on(ctx);
}

/* Device constant configuration parameters */
struct uart_sam0_dev_cfg {
	SercomUsart *regs;
};

static int bc_uart_sam0_fifo_read(const struct device *dev, uint8_t *rx_data, const int size,
				  bool *bit9)
{
	const struct uart_sam0_dev_cfg *config = dev->config;
	SercomUsart *const regs = config->regs;

	if (regs->INTFLAG.bit.RXC) {
		uint16_t ch = regs->DATA.reg;

		if (size >= 1) {
			*bit9 = (ch & 0x100) != 0;
			*rx_data = ch;
			return 1;
		} else {
			return -EINVAL;
		}
	}
	return 0;
}

/*
 * A byte has been received from a serial port. We just store it in the buffer
 * for processing when a complete packet has been received.
 */
static void cb_handler_rx(struct bcbus_context *ctx)
{
	struct bcbus_serial_config *cfg = ctx->cfg;

	int n;
	bool dummy; /* ignore bit9 revieved */

	if (cfg->uart_buf_ctr == CONFIG_BCBUS_BUFFER_SIZE) {
		/* Buffer full. Disable interrupt until timeout. */
		bcbus_serial_rx_disable(ctx);
		return;
	}

	/* Restart timer on a new character */
	k_timer_start(&cfg->rtu_timer, K_USEC(cfg->rtu_timeout), K_NO_WAIT);

	n = bc_uart_sam0_fifo_read(cfg->dev, cfg->uart_buf_ptr,
				   (CONFIG_BCBUS_BUFFER_SIZE - cfg->uart_buf_ctr), &dummy);

	if (cfg->uart_bit9) {
		/* BAdr Echo recieved */
		k_timer_stop(&cfg->rtu_timer);
		k_work_submit(&ctx->server_work);
		return;
	}

	if (cfg->uart_buf_ctr == 0) {
		cfg->databytes_to_read = *cfg->uart_buf_ptr;
	}

	cfg->uart_buf_ptr += n;
	cfg->uart_buf_ctr += n;

	if (cfg->uart_buf_ctr == cfg->databytes_to_read + 3) {
		/* Datablock ready */
		k_timer_stop(&cfg->rtu_timer);
		k_work_submit(&ctx->server_work);
		return;
	}
}

static int bc_uart_sam0_fifo_fill(const struct device *dev, const uint8_t *tx_data, int len,
				  bool bit9)
{
	const struct uart_sam0_dev_cfg *config = dev->config;
	SercomUsart *regs = config->regs;

	if (regs->INTFLAG.bit.DRE && len >= 1) {
		regs->DATA.reg = (bit9 ? 0x100 : 0x0) | tx_data[0];
		return 1;
	} else {
		return 0;
	}
}

static void cb_handler_tx(struct bcbus_context *ctx)
{
	struct bcbus_serial_config *cfg = ctx->cfg;
	int n;

	if (cfg->uart_buf_ctr > 0) {
		n = bc_uart_sam0_fifo_fill(cfg->dev, cfg->uart_buf_ptr, cfg->uart_buf_ctr,
					   cfg->uart_bit9);
		cfg->uart_buf_ctr -= n;
		cfg->uart_buf_ptr += n;
		return;
	}

	/* Must wait till the transmission is complete or
	 * RS-485 transceiver could be disabled before all data has
	 * been transmitted and message will be corrupted.
	 */
	if (uart_irq_tx_complete(cfg->dev)) {
		/* Disable transmission */
		cfg->uart_buf_ptr = &cfg->uart_buf[0];
		bcbus_serial_tx_off(ctx);
		bcbus_serial_rx_on(ctx);
	}
}

static void uart_cb_handler(const struct device *dev, void *app_data)
{
	struct bcbus_context *ctx = (struct bcbus_context *)app_data;

	if (ctx == NULL) {
		LOG_ERR("Bus hardware is not properly initialized");
		return;
	}

	if (uart_irq_update(dev) && uart_irq_is_pending(dev)) {

		if (uart_irq_rx_ready(dev)) {
			cb_handler_rx(ctx);
		}

		if (uart_irq_tx_ready(dev)) {
			cb_handler_tx(ctx);
		}
	}
}

/* This function is called when the RTU framing timer expires. */
static void rtu_tmr_handler(struct k_timer *t_id)
{
	struct bcbus_context *ctx;

	ctx = (struct bcbus_context *)k_timer_user_data_get(t_id);

	if (ctx == NULL) {
		LOG_ERR("Failed to get Bus context");
		return;
	}

	k_work_submit(&ctx->server_work);
}

static int configure_gpio(struct bcbus_context *ctx)
{
	struct bcbus_serial_config *cfg = ctx->cfg;

	if (cfg->de != NULL) {
		if (!device_is_ready(cfg->de->port)) {
			return -ENODEV;
		}

		if (gpio_pin_configure_dt(cfg->de, GPIO_OUTPUT_INACTIVE)) {
			return -EIO;
		}
	}

	if (cfg->re != NULL) {
		if (!device_is_ready(cfg->re->port)) {
			return -ENODEV;
		}

		if (gpio_pin_configure_dt(cfg->re, GPIO_OUTPUT_INACTIVE)) {
			return -EIO;
		}
	}

	return 0;
}

static inline int configure_uart(struct bcbus_context *ctx, struct bcbus_iface_param *param)
{
	struct bcbus_serial_config *cfg = ctx->cfg;
	struct uart_config uart_cfg = {
		.baudrate = param->serial.baud,
		.flow_ctrl = UART_CFG_FLOW_CTRL_NONE,
	};

	uart_cfg.data_bits = UART_CFG_DATA_BITS_9;

	uart_cfg.parity = param->serial.parity;
	uart_cfg.stop_bits = UART_CFG_STOP_BITS_1;

	if (uart_configure(cfg->dev, &uart_cfg) != 0) {
		LOG_ERR("Failed to configure UART");
		return -EINVAL;
	}

	return 0;
}

void bcbus_serial_rx_disable(struct bcbus_context *ctx)
{
	bcbus_serial_rx_off(ctx);
}

void bcbus_serial_rx_enable(struct bcbus_context *ctx)
{
	bcbus_serial_rx_on(ctx);
}

int bcbus_serial_rx_adu(struct bcbus_context *ctx)
{
	struct bcbus_serial_config *cfg = ctx->cfg;
	int rc = 0;

	rc = bcbus_rtu_rx_adu(ctx);

	cfg->uart_buf_ctr = 0;
	cfg->uart_buf_ptr = &cfg->uart_buf[0];

	k_sleep(K_USEC(2000));

	return rc;
}

int bcbus_serial_tx_adu(struct bcbus_context *ctx)
{
	rtu_tx_adu(ctx);
	return 0;
}

int bcbus_serial_tx_ba(struct bcbus_context *ctx)
{
	rtu_tx_ba(ctx);
	return 0;
}

int bcbus_serial_init(struct bcbus_context *ctx, struct bcbus_iface_param param)
{
	struct bcbus_serial_config *cfg = ctx->cfg;
	const uint32_t if_delay_max = 3500000;
	const uint32_t numof_bits = 11;
	int err;

	if (!device_is_ready(cfg->dev)) {
		LOG_ERR("Bus device %s is not ready", cfg->dev->name);
		return -ENODEV;
	}

	if (IS_ENABLED(CONFIG_UART_USE_RUNTIME_CONFIGURE)) {
		if (configure_uart(ctx, &param) != 0) {
			return -EINVAL;
		}
	}

	if (param.serial.baud <= 38400) {
		cfg->rtu_timeout = (numof_bits * if_delay_max) / param.serial.baud;
	} else {
		cfg->rtu_timeout = (numof_bits * if_delay_max) / 38400;
	}

	cfg->check_ba_echo = false;
	cfg->adjust_ba_time = false;
	switch (param.serial.bcbus_cfg_ba_timing) {
	case BCBUS_UART_CFG_CHECKS_FULL:
		cfg->check_ba_echo = true;
	case BCBUS_UART_CFG_BA_SHRINK:
		cfg->adjust_ba_time = true;
		break;
	case BCBUS_UART_CFG_CHECK_BA:
		cfg->check_ba_echo = true;
		break;
	case BCBUS_UART_CFG_CHECKS_NONE:
	default:;
	}

	if (configure_gpio(ctx) != 0) {
		return -EIO;
	}

	cfg->uart_buf_ctr = 0;
	cfg->uart_buf_ptr = &cfg->uart_buf[0];

	err = uart_irq_callback_user_data_set(cfg->dev, uart_cb_handler, ctx);
	if (err != 0) {
		return err;
	};

	k_timer_init(&cfg->rtu_timer, rtu_tmr_handler, NULL);
	k_timer_user_data_set(&cfg->rtu_timer, ctx);

	bcbus_serial_rx_on(ctx);
	LOG_INF("RTU timeout %u us", cfg->rtu_timeout);

	return 0;
}

void bcbus_serial_disable(struct bcbus_context *ctx)
{
	bcbus_serial_tx_off(ctx);
	bcbus_serial_rx_off(ctx);
	k_timer_stop(&ctx->cfg->rtu_timer);
}
