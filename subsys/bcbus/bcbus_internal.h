/*
 * Copyright (c) 2020 PHYTEC Messtechnik GmbH
 * Copyright (c) 2021 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Parts of this file are based on mb.h from uC/Modbus Stack.
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

#ifndef ZEPHYR_INCLUDE_BCBUS_INTERNAL_H_
#define ZEPHYR_INCLUDE_BCBUS_INTERNAL_H_

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/bcbus/bcbus.h>
#include <zephyr/sys/ring_buffer.h>

#ifdef CONFIG_BCBUS_FP_EXTENSIONS
#define BCBUS_FP_EXTENSIONS_ADDR 5000
#else
#define BCBUS_FP_EXTENSIONS_ADDR UINT16_MAX
#endif

#define BCBUS_RTU_MTU 256

/* Modbus function codes */
#define BCBUS_FC01_COIL_RD         1
#define BCBUS_FC02_DI_RD           2
#define BCBUS_FC03_HOLDING_REG_RD  3
#define BCBUS_FC04_IN_REG_RD       4
#define BCBUS_FC05_COIL_WR         5
#define BCBUS_FC06_HOLDING_REG_WR  6
#define BCBUS_FC08_DIAGNOSTICS     8
#define BCBUS_FC15_COILS_WR        15
#define BCBUS_FC16_HOLDING_REGS_WR 16

/* Diagnostic sub-function codes */
#define BCBUS_FC08_SUBF_QUERY              0
#define BCBUS_FC08_SUBF_CLR_CTR            10
#define BCBUS_FC08_SUBF_BUS_MSG_CTR        11
#define BCBUS_FC08_SUBF_BUS_CRC_CTR        12
#define BCBUS_FC08_SUBF_BUS_EXCEPT_CTR     13
#define BCBUS_FC08_SUBF_SERVER_MSG_CTR     14
#define BCBUS_FC08_SUBF_SERVER_NO_RESP_CTR 15

/* Modbus RTU (ASCII) constants */
#define BCBUS_COIL_OFF_CODE          0x0000
#define BCBUS_COIL_ON_CODE           0xFF00
#define BCBUS_RTU_MIN_MSG_SIZE       3
#define BCBUS_CRC16_POLY             0xA001
#define BCBUS_ASCII_MIN_MSG_SIZE     11
#define BCBUS_ASCII_START_FRAME_CHAR ':'
#define BCBUS_ASCII_END_FRAME_CHAR1  '\r'
#define BCBUS_ASCII_END_FRAME_CHAR2  '\n'

/* Modbus ADU constants */
#define BCBUS_ADU_PROTO_ID 0x0000

#define RING_BUF_DECLARE_DYN(name, size8)                                                          \
	BUILD_ASSERT(size8 <= RING_BUFFER_MAX_SIZE, RING_BUFFER_SIZE_ASSERT_MSG);                  \
	uint8_t _ring_buffer_data_##name[size8];                                                   \
	struct ring_buf name;

struct bcbus_serial_config {
	/* UART device */
	const struct device *dev;
	/* RTU timeout (maximum inter-frame delay) */
	uint32_t rtu_timeout;
	/* Pointer to current position in buffer */
	uint8_t *uart_buf_ptr;
	/* Pointer to driver enable (DE) pin config */
	struct gpio_dt_spec *de;
	/* Pointer to receiver enable (nRE) pin config */
	struct gpio_dt_spec *re;
	/* RTU timer to detect frame end point */
	struct k_timer rtu_timer;
	/* Number of bytes received or to send */
	uint16_t uart_buf_ctr;
	/* lenght of dataspace in tele */
	uint8_t databytes_to_read;
	/* Storage of received characters or characters to send */
	uint8_t uart_buf[CONFIG_BCBUS_BUFFER_SIZE + 4];
	/* Bit 9 */
	bool uart_bit9;
	bool check_ba_echo;
	bool adjust_ba_time;
};

#define BCBUS_STATE_CONFIGURED 0

struct bcbus_context {
	/* Interface name */
	const char *iface_name;
	union {
		/* Serial line configuration */
		struct bcbus_serial_config *cfg;
		/* RAW TX callback */
		struct bcbus_raw_cb rawcb;
	};
	/* True if interface is configured as client */
	bool client;
	/* Amount of time client is willing to wait for response from server */
	uint32_t rxwait_to;
	/* Amount of time client is willing to wait for response from server */
	uint32_t baecho_to;
	/* Pointer to user server callbacks */
	struct bcbus_user_callbacks *mbs_user_cb;
	/* Interface state */
	atomic_t state;

	/* Client's mutually exclusive access */
	struct k_mutex iface_lock;
	/* Wait for response semaphore */
	struct k_sem client_wait_sem;
	/* Server work item */
	struct k_work server_work;
	/* Received frame */
	struct bcbus_adu rx_adu;
	/* Frame to transmit */
	struct bcbus_adu tx_adu;

	/* Records error from frame reception, e.g. CRC error */
	int rx_adu_err;

#ifdef CONFIG_BCBUS_FC08_DIAGNOSTIC
	uint16_t mbs_msg_ctr;
	uint16_t mbs_crc_err_ctr;
	uint16_t mbs_except_ctr;
	uint16_t mbs_server_msg_ctr;
	uint16_t mbs_noresp_ctr;
#endif
	/* A linked list of function code, handler pairs */
	sys_slist_t user_defined_cbs;
	/* Unit ID */
	uint8_t unit_id;
	/* buffers for tty */
	RING_BUF_DECLARE_DYN(tty_sbuf, 80);
	RING_BUF_DECLARE_DYN(tty_rbuf, 80);
	uint8_t tty_id;
	uint8_t slavebuffree;

	struct bcbus_obj *bus_item_prop_list;
};

/**
 * @brief Get Modbus interface context.
 *
 * @param ctx        Modbus interface context
 *
 * @retval           Pointer to interface context or NULL
 *                   if interface not available or not configured;
 */
struct bcbus_context *bcbus_get_context(const uint8_t iface);

/**
 * @brief Get BC-bus interface index.
 *
 * @param ctx        Pointer to Modbus interface context
 *
 * @retval           Interface index or negative error value.
 */
int bcbus_iface_get_by_ctx(const struct bcbus_context *ctx);

/**
 * @brief Send ADU.
 *
 * @param ctx        Modbus interface context
 */
void bcbus_tx_adu(struct bcbus_context *ctx);

/**
 * @brief Send BAdr with bit9.
 *
 * @param ctx        BCbus interface context
 */
void bcbus_tx_ba(struct bcbus_context *ctx);

/**
 * @brief Send BAdr and wait for echo
 *
 * @param ctx        BCbus interface context
 *
 * @retval           0 If the function was successful,
 *                   -ENOTSUP if BCbus mode is not supported,
 *                   -ETIMEDOUT on timeout,
 */
int bcbus_check_unit_present(struct bcbus_context *ctx);

/**
 * @brief Send ADU and wait certain time for response.
 *
 * @param ctx        Modbus interface context
 *
 * @retval           0 If the function was successful,
 *                   -ENOTSUP if Modbus mode is not supported,
 *                   -ETIMEDOUT on timeout,
 *                   -EMSGSIZE on length error,
 *                   -EIO on CRC error.
 */
int bcbus_tx_wait_rx_adu(struct bcbus_context *ctx);

/**
 * @brief Let server handle the received ADU.
 *
 * @param ctx        Modbus interface context
 *
 * @retval           True if the server has prepared a response ADU
 *                   that should be sent.
 */
bool bcbus_server_handler(struct bcbus_context *ctx);

/**
 * @brief Reset server stats.
 *
 * @param ctx        Modbus interface context
 */
void bcbus_reset_stats(struct bcbus_context *ctx);

/**
 * @brief Disable serial line reception.
 *
 * @param ctx        Modbus interface context
 */
void bcbus_serial_rx_disable(struct bcbus_context *ctx);

/**
 * @brief Enable serial line reception.
 *
 * @param ctx        Modbus interface context
 */
void bcbus_serial_rx_enable(struct bcbus_context *ctx);

/**
 * @brief Assemble ADU from serial line RX buffer
 *
 * @param ctx        Modbus interface context
 *
 * @retval           0 If the function was successful,
 *                   -ENOTSUP if serial line mode is not supported,
 *                   -EMSGSIZE on length error,
 *                   -EIO on CRC error.
 */
int bcbus_serial_rx_adu(struct bcbus_context *ctx);

/**
 * @brief Assemble ADU from serial line RX buffer
 *
 * @param ctx        Modbus interface context
 *
 * @retval           0 If the function was successful,
 *                   -ENOTSUP if serial line mode is not supported.
 */
int bcbus_serial_tx_adu(struct bcbus_context *ctx);

/**
 * @brief Send unit BA with 9bit
 *
 * @param ctx        BCbus interface context
 *
 * @retval           0 If the function was successful,
 *                   -ENOTSUP if serial line mode is not supported.
 */
int bcbus_serial_tx_ba(struct bcbus_context *ctx);

/**
 * @brief Initialize serial line support.
 *
 * @param ctx        Modbus interface context
 * @param param      Configuration parameter of the interface
 *
 * @retval           0 If the function was successful.
 */
int bcbus_serial_init(struct bcbus_context *ctx, struct bcbus_iface_param param);

/**
 * @brief Disable serial line support.
 *
 * @param ctx        Modbus interface context
 */
void bcbus_serial_disable(struct bcbus_context *ctx);

int bcbus_raw_rx_adu(struct bcbus_context *ctx);
int bcbus_raw_tx_adu(struct bcbus_context *ctx);
int bcbus_raw_init(struct bcbus_context *ctx, struct bcbus_iface_param param);

#endif /* ZEPHYR_INCLUDE_BCBUS_INTERNAL_H_ */
