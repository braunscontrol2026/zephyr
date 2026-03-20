/*
 * Copyright (c) 2020 PHYTEC Messtechnik GmbH
 * Copyright (c) 2021 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Client API in this file is based on mbm_core.c from uC/Modbus Stack.
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

/**
 * @brief BCBUS transport protocol API
 * @defgroup bcbus BCBUS
 * @ingroup io_interfaces
 * @{
 */

#ifndef ZEPHYR_INCLUDE_BCBUS_H_
#define ZEPHYR_INCLUDE_BCBUS_H_

#include <zephyr/drivers/uart.h>
#include <zephyr/sys/slist.h>
#ifdef __cplusplus
extern "C" {
#endif

/** Length of MBAP Header */
#define BCBUS_MBAP_LENGTH        7
/** Length of MBAP Header plus function code */
#define BCBUS_MBAP_AND_FC_LENGTH (BCBUS_MBAP_LENGTH + 1)

/**
 * @brief		maximum raw data size without overhead.
 */
#define BUS_MAX_DATA    16
#define BUS_MAX_DATA_V2 64

/** @name BCbus token
 *  @{
 */

#define RG_TYPE   1
#define UG_TYPE   2
#define SR_TYPE   3
#define SR2_TYPE  4
#define FG_TYPE   5
#define VS_TYPE   6
#define LG_TYPE   7
#define KS_TYPE   8
#define CR1_TYPE  9
#define CR2_TYPE  10
#define RGLI_TYPE 11
#define CR3_TYPE  12

#define FW_SUBTYPE  190
#define FW2_SUBTYPE 191

#define PROC_FLASH 0x30

#define SEND_SER_NUM  0x40
#define OUTVARS       0x50
#define SEND_RTEMP    0x50
#define SEND_VTEMP    0x50
#define SEND_ANAIN    0x50
#define SEND_LSTAT    0x50
#define SEND_RLTEMP   0x51
#define SEND_VENTIL   0x52
#define SEND_PWM      0x52
#define SEND_RTEMP_S  0x53
#define SEND_VTEMP_S  0x53
#define SEND_DO       0x53
#define SEND_RLTEMP_S 0x54
#define SEND_ANAO     0x54
#define SEND_ATEMP    0x55
#define SEND_3PO      0x55
#define SEND_SHIFT_S  0x55
#define SEND_PUMP     0x56
#define SEND_DI       0x56
#define SEND_VENT     0x56
#define SEND_BRSTAT   0x56

#define INVARS        0x60
#define SET_RTEMP_S   0x60
#define SET_VTEMP_S   0x60
#define SETZE_SOLL    0x60
#define SET_LSOLL     0x60
#define SET_RTEMP_SC  0x61
#define SET_CRTAST    0x61
#define SET_LTAST     0x61
#define SET_RLTEMP_S  0x62
#define SET_BETR_STAT 0x63
#define SET_COMFORT_S 0x63
#define SET_VERBUND   0x64

#define SEND_XDATA 0x70

#define SEND_STATUS 0x73

#define SEND_BETRSTD 0x74
#define SEND_COUNTER 0x74

#define WRITE_NVRAM    0x80
#define WRITE_RTC      0x81
#define PARA_CMD       0x82
#define TTY_CALL       0x83
#define SET_KEY        0xE0
#define SET_STATBITS   0xE1
#define RES_STATBITS   0xE2
#define SET_STATUS     0xE3
#define REC_XBUF       0xF2
#define SET_BUSADR_SER 0xF3
#define BLK_XCHG       0xF4
//---------------------------------------------------------------------------
/* second token for PARA-Commands */
/* reset polling pointer: PARA_CMD, PARA_RST - answer: none */
#define PARA_RST       0x10
/* poll next pointer: PARA_CMD, PARA_POLL
 * - answer: PARA_CMD, PARA_VAL, <para_numl>, <para_numh>, <para_vall>, <para_valh> */
#define PARA_POLL      0x11 /* get next parameter */
/* write para: PARA_CMD, PARA_SET, <para_numl>, <para_numh>, <para_vall>, <para_valh>
 * - answer: PARA_CMD, PARA_STAT, <para_stat>
 * * <para_stat> 0: OK, 1: unknown */
#define PARA_SET       0x12 /* write parameter */
/* PARA_CMD, PARA_GET, <para_numl>, <para_numh> *
 * - answer: PARA_CMD, PARA_VAL, <para_numl>, <para_numh>, <para_vall>, <para_valh> */
#define PARA_GET       0x13 /* read parameter */
/* PARA_CMD, PARA_STAT, <para_stat> *
 * * <para_stat> 0: OK, 1: fail */
#define PARA_STAT      0x14
/* - answer: PARA_CMD, PARA_VAL, <para_numl>, <para_numh>, <para_vall>, <para_valh> */
#define PARA_VAL       0x15
/** @} */

/** @name Modbus exception codes
 *  @{
 */
/** No exception */
#define BCBUS_EXC_NONE                     0
/** Illegal function code */
#define BCBUS_EXC_ILLEGAL_FC               1
/** Illegal data address */
#define BCBUS_EXC_ILLEGAL_DATA_ADDR        2
/** Illegal data value */
#define BCBUS_EXC_ILLEGAL_DATA_VAL         3
/** Server device failure */
#define BCBUS_EXC_SERVER_DEVICE_FAILURE    4
/** Acknowledge */
#define BCBUS_EXC_ACK                      5
/** Server device busy */
#define BCBUS_EXC_SERVER_DEVICE_BUSY       6
/** Memory parity error */
#define BCBUS_EXC_MEM_PARITY_ERROR         8
/** Gateway path unavailable */
#define BCBUS_EXC_GW_PATH_UNAVAILABLE      10
/** Gateway target device failed to respond */
#define BCBUS_EXC_GW_TARGET_FAILED_TO_RESP 11
/** @} */

/**
 * @brief BC telegram struct used for handshake
 */
struct bcbus_tele {
	/** txbuffer */
	uint8_t txbuf[CONFIG_BCBUS_BUFFER_SIZE + 2];
	/** rxbuffer */
	uint8_t rxbuf[CONFIG_BCBUS_BUFFER_SIZE + 2];
};

/**
 * @brief Frame struct used internally and for raw ADU support.
 */
struct bcbus_adu {
	/** Transaction Identifier */
	uint16_t trans_id;
	/** Protocol Identifier */
	uint16_t proto_id;
	/** Length of the data only (not the length of unit ID + PDU) */
	uint16_t length;
	/** Unit Identifier */
	uint8_t unit_id;
	/** Function Code */
	uint8_t fc;
	/** Transaction Data */
	uint8_t data[CONFIG_BCBUS_BUFFER_SIZE - 3];
	/** RTU CRC */
	uint16_t crc;
};

/**
 * @brief do handshake on BC Bus
 *
 * Sends a telegram an bus and revieves answer.
 *
 * @param iface      BCbus interface index
 * @param unit_id    BCbus BusAddress
 * @param buf        Pointer to telegram buffer
 *
 * @retval           0 If the function was successful
 */
int bcbus_do_client_handshake(const int iface, const uint8_t unit_id, struct bcbus_tele *buf);

/**
 * @brief put tty data and poll on BC Bus
 *
 * Sends a tty telegram on bus and recieves answer.
 *
 * @param iface      BCbus interface index
 * @param unit_id    BCbus BusAddress
 * @param data Address of the output buffer. Can be NULL to discard data.
 * @param size Data size (in bytes).
 *
 * @retval Number of bytes written to the output buffer.
 */
uint32_t bcbus_tty_client_put(const int iface, const uint8_t unit_id, uint8_t *data, uint32_t size);

int bcbus_tty_client_new_connect(const int iface);

/**
 * @brief Find interface index from busaddr.
 *
 * @param ba        Busaddr searching
 *
 * @retval           Interface index or negative error value.
 */
int bcbus_find_ba_iface(const uint8_t ba);

/**
 * @brief Read data from a ring buffer.
 *
 * This routine reads data from the tty ring buffer @a iface.
 *
 * @warning
 * Use cases involving multiple reads of the ring buffer must prevent
 * concurrent read operations, either by preventing all readers from
 * being preempted or by using a mutex to govern reads to the ring buffer.
 *
 * @warning
 * Ring buffer instance should not mix byte access and  item mode
 * (calls prefixed with ring_buf_item_).
 *
 * @param iface  Interface number of bcbus
 * @param unit_id    BCbus BusAddress
 * @param data Address of the output buffer. Can be NULL to discard data.
 * @param size Data size (in bytes).
 *
 * @retval Number of bytes written to the output buffer.
 */
uint32_t bcbus_tty_client_get(const int iface, const uint8_t unit_id, uint8_t *data, uint32_t size);

int bcbus_tty_client_poll(const int iface);

/**
 * @brief Gets a copy from polling list.
 *
 * This routine uses @a iface.
 *
 * @warning
 * This Routine allocates memory from heap for list. User must
 * free allocated memory after use.
 *
 * @param iface  Interface number of bcbus
 *
 * @retval NULL on error.
 */
struct bcbus_obj *bcbus_get_copy_poll_list(const int iface);

/**
 * @brief Replaces polling list with newlist.
 *
 * This routine uses @a iface.
 *
 * @param iface  Interface number of bcbus
 *
 * @param newlist Allocated new list
 *
 * @retval 0 on success.
 */
int bcbus_replace_poll_list(const int iface, struct bcbus_obj *newlist);

/**
 * @brief Coil read (FC01)
 *
 * Sends a Modbus message to read the status of coils from a server.
 *
 * @param iface      Modbus interface index
 * @param unit_id    Modbus unit ID of the server
 * @param start_addr Coil starting address
 * @param coil_tbl   Pointer to an array of bytes containing the value
 *                   of the coils read.
 *                   The format is:
 *
 *                                       MSB                               LSB
 *                                       B7   B6   B5   B4   B3   B2   B1   B0
 *                                       -------------------------------------
 *                       coil_tbl[0]     #8   #7                            #1
 *                       coil_tbl[1]     #16  #15                           #9
 *                            :
 *                            :
 *
 *                   Note that the array that will be receiving the coil
 *                   values must be greater than or equal to:
 *                   (num_coils - 1) / 8 + 1
 * @param num_coils  Quantity of coils to read
 *
 * @retval           0 If the function was successful
 */
int bcbus_read_coils(const int iface, const uint8_t unit_id, const uint16_t start_addr,
		     uint8_t *const coil_tbl, const uint16_t num_coils);

/**
 * @brief Read discrete inputs (FC02)
 *
 * Sends a Modbus message to read the status of discrete inputs from
 * a server.
 *
 * @param iface      Modbus interface index
 * @param unit_id    Modbus unit ID of the server
 * @param start_addr Discrete input starting address
 * @param di_tbl     Pointer to an array that will receive the state
 *                   of the discrete inputs.
 *                   The format of the array is as follows:
 *
 *                                     MSB                               LSB
 *                                     B7   B6   B5   B4   B3   B2   B1   B0
 *                                     -------------------------------------
 *                       di_tbl[0]     #8   #7                            #1
 *                       di_tbl[1]     #16  #15                           #9
 *                            :
 *                            :
 *
 *                   Note that the array that will be receiving the discrete
 *                   input values must be greater than or equal to:
 *                        (num_di - 1) / 8 + 1
 * @param num_di     Quantity of discrete inputs to read
 *
 * @retval           0 If the function was successful
 */
int bcbus_read_dinputs(const int iface, const uint8_t unit_id, const uint16_t start_addr,
		       uint8_t *const di_tbl, const uint16_t num_di);

/**
 * @brief Read holding registers (FC03)
 *
 * Sends a Modbus message to read the value of holding registers
 * from a server.
 *
 * @param iface      Modbus interface index
 * @param unit_id    Modbus unit ID of the server
 * @param start_addr Register starting address
 * @param reg_buf    Is a pointer to an array that will receive
 *                   the current values of the holding registers from
 *                   the server.  The array pointed to by 'reg_buf' needs
 *                   to be able to hold at least 'num_regs' entries.
 * @param num_regs   Quantity of registers to read
 *
 * @retval           0 If the function was successful
 */
int bcbus_read_holding_regs(const int iface, const uint8_t unit_id, const uint16_t start_addr,
			    uint16_t *const reg_buf, const uint16_t num_regs);

/**
 * @brief Read input registers (FC04)
 *
 * Sends a Modbus message to read the value of input registers from
 * a server.
 *
 * @param iface      Modbus interface index
 * @param unit_id    Modbus unit ID of the server
 * @param start_addr Register starting address
 * @param reg_buf    Is a pointer to an array that will receive
 *                   the current value of the holding registers
 *                   from the server.  The array pointed to by 'reg_buf'
 *                   needs to be able to hold at least 'num_regs' entries.
 * @param num_regs   Quantity of registers to read
 *
 * @retval           0 If the function was successful
 */
int bcbus_read_input_regs(const int iface, const uint8_t unit_id, const uint16_t start_addr,
			  uint16_t *const reg_buf, const uint16_t num_regs);

/**
 * @brief Write single coil (FC05)
 *
 * Sends a Modbus message to write the value of single coil to a server.
 *
 * @param iface      Modbus interface index
 * @param unit_id    Modbus unit ID of the server
 * @param coil_addr  Coils starting address
 * @param coil_state Is the desired state of the coil
 *
 * @retval           0 If the function was successful
 */
int bcbus_write_coil(const int iface, const uint8_t unit_id, const uint16_t coil_addr,
		     const bool coil_state);

/**
 * @brief Write single holding register (FC06)
 *
 * Sends a Modbus message to write the value of single holding register
 * to a server unit.
 *
 * @param iface      Modbus interface index
 * @param unit_id    Modbus unit ID of the server
 * @param start_addr Coils starting address
 * @param reg_val    Desired value of the holding register
 *
 * @retval           0 If the function was successful
 */
int bcbus_write_holding_reg(const int iface, const uint8_t unit_id, const uint16_t start_addr,
			    const uint16_t reg_val);

/**
 * @brief Read diagnostic (FC08)
 *
 * Sends a Modbus message to perform a diagnostic function of a server unit.
 *
 * @param iface      Modbus interface index
 * @param unit_id    Modbus unit ID of the server
 * @param sfunc      Diagnostic sub-function code
 * @param data       Sub-function data
 * @param data_out   Pointer to the data value
 *
 * @retval           0 If the function was successful
 */
int bcbus_request_diagnostic(const int iface, const uint8_t unit_id, const uint16_t sfunc,
			     const uint16_t data, uint16_t *const data_out);

/**
 * @brief Write coils (FC15)
 *
 * Sends a Modbus message to write to coils on a server unit.
 *
 * @param iface      Modbus interface index
 * @param unit_id    Modbus unit ID of the server
 * @param start_addr Coils starting address
 * @param coil_tbl   Pointer to an array of bytes containing the value
 *                   of the coils to write.
 *                   The format is:
 *
 *                                       MSB                               LSB
 *                                       B7   B6   B5   B4   B3   B2   B1   B0
 *                                       -------------------------------------
 *                       coil_tbl[0]     #8   #7                            #1
 *                       coil_tbl[1]     #16  #15                           #9
 *                            :
 *                            :
 *
 *                   Note that the array that will be receiving the coil
 *                   values must be greater than or equal to:
 *                   (num_coils - 1) / 8 + 1
 * @param num_coils  Quantity of coils to write
 *
 * @retval           0 If the function was successful
 */
int bcbus_write_coils(const int iface, const uint8_t unit_id, const uint16_t start_addr,
		      uint8_t *const coil_tbl, const uint16_t num_coils);

/**
 * @brief Write holding registers (FC16)
 *
 * Sends a Modbus message to write to integer holding registers
 * to a server unit.
 *
 * @param iface      Modbus interface index
 * @param unit_id    Modbus unit ID of the server
 * @param start_addr Register starting address
 * @param reg_buf    Is a pointer to an array containing
 *                   the value of the holding registers to write.
 *                   Note that the array containing the register values must
 *                   be greater than or equal to 'num_regs'
 * @param num_regs   Quantity of registers to write
 *
 * @retval           0 If the function was successful
 */
int bcbus_write_holding_regs(const int iface, const uint8_t unit_id, const uint16_t start_addr,
			     uint16_t *const reg_buf, const uint16_t num_regs);

/**
 * @brief Read floating-point holding registers (FC03)
 *
 * Sends a Modbus message to read the value of floating-point
 * holding registers from a server unit.
 *
 * @param iface      Modbus interface index
 * @param unit_id    Modbus unit ID of the server
 * @param start_addr Register starting address
 * @param reg_buf    Is a pointer to an array that will receive
 *                   the current values of the holding registers from
 *                   the server.  The array pointed to by 'reg_buf' needs
 *                   to be able to hold at least 'num_regs' entries.
 * @param num_regs   Quantity of registers to read
 *
 * @retval           0 If the function was successful
 */
int bcbus_read_holding_regs_fp(const int iface, const uint8_t unit_id, const uint16_t start_addr,
			       float *const reg_buf, const uint16_t num_regs);

/**
 * @brief Write floating-point holding registers (FC16)
 *
 * Sends a Modbus message to write to floating-point holding registers
 * to a server unit.
 *
 * @param iface      Modbus interface index
 * @param unit_id    Modbus unit ID of the server
 * @param start_addr Register starting address
 * @param reg_buf    Is a pointer to an array containing
 *                   the value of the holding registers to write.
 *                   Note that the array containing the register values must
 *                   be greater than or equal to 'num_regs'
 * @param num_regs   Quantity of registers to write
 *
 * @retval           0 If the function was successful
 */
int bcbus_write_holding_regs_fp(const int iface, const uint8_t unit_id, const uint16_t start_addr,
				float *const reg_buf, const uint16_t num_regs);

/** Modbus Server User Callback structure */
struct bcbus_user_callbacks {
	/** Coil read callback */
	int (*coil_rd)(uint16_t addr, bool *state);

	/** Coil write callback */
	int (*coil_wr)(uint16_t addr, bool state);

	/** Discrete Input read callback */
	int (*discrete_input_rd)(uint16_t addr, bool *state);

	/** Input Register read callback */
	int (*input_reg_rd)(uint16_t addr, uint16_t *reg);

	/** Floating Point Input Register read callback */
	int (*input_reg_rd_fp)(uint16_t addr, float *reg);

	/** Holding Register read callback */
	int (*holding_reg_rd)(uint16_t addr, uint16_t *reg);

	/** Holding Register write callback */
	int (*holding_reg_wr)(uint16_t addr, uint16_t reg);

	/** Floating Point Holding Register read callback */
	int (*holding_reg_rd_fp)(uint16_t addr, float *reg);

	/** Floating Point Holding Register write callback */
	int (*holding_reg_wr_fp)(uint16_t addr, float reg);
};

/**
 * @brief Get Modbus interface index according to interface name
 *
 * If there is more than one interface, it can be used to clearly
 * identify interfaces in the application.
 *
 * @param iface_name Modbus interface name
 *
 * @retval           Modbus interface index or negative error value.
 */
int bcbus_iface_get_by_name(const char *iface_name);

/**
 * @brief ADU raw callback function signature
 *
 * @param iface      Modbus RTU interface index
 * @param adu        Pointer to the RAW ADU struct to send
 * @param user_data  Pointer to the user data
 *
 * @retval           0 If transfer was successful
 */
typedef int (*bcbus_raw_cb_t)(const int iface, const struct bcbus_adu *adu, void *user_data);

/**
 * @brief Custom function code handler function signature.
 *
 * Modbus allows user defined function codes which can be used to extend
 * the base protocol. These callbacks can also be used to implement
 * function codes currently not supported by Zephyr's Modbus subsystem.
 *
 * If an error occurs during the handling of the request, the handler should
 * signal this by setting excep_code to a bcbus exception code.
 *
 * User data pointer can be used to pass state between subsequent calls to
 * the handler.
 *
 * @param iface      Modbus interface index
 * @param rx_adu     Pointer to the received ADU struct
 * @param tx_adu     Pointer to the outgoing ADU struct
 * @param excep_code Pointer to possible exception code
 * @param user_data  Pointer to user data
 *
 * @retval           true If response should be sent, false otherwise
 */
typedef bool (*bcbus_custom_cb_t)(const int iface, const struct bcbus_adu *const rx_adu,
				  struct bcbus_adu *const tx_adu, uint8_t *const excep_code,
				  void *const user_data);

/** @cond INTERNAL_HIDDEN */
/**
 * @brief Custom function code definition.
 */
struct bcbus_custom_fc {
	sys_snode_t node;
	bcbus_custom_cb_t cb;
	void *user_data;
	uint8_t fc;
	uint8_t excep_code;
};
/** @endcond INTERNAL_HIDDEN */

/**
 * @brief Helper macro for initializing custom function code structs
 */
#define BCBUS_CUSTOM_FC_DEFINE(name, user_cb, user_fc, userdata)                                   \
	static struct bcbus_custom_fc bcbus_cfg_##name = {                                         \
		.cb = user_cb,                                                                     \
		.user_data = userdata,                                                             \
		.fc = user_fc,                                                                     \
		.excep_code = BCBUS_EXC_NONE,                                                      \
	}

/** @brief bcbus mode */
enum bcbus_uart_config_ba {
	BCBUS_UART_CFG_CHECKS_NONE, /**< no ba timing adjusts, no echo checks */
	BCBUS_UART_CFG_BA_SHRINK,   /**< ba timing adjust, no echo checks */
	BCBUS_UART_CFG_CHECK_BA,    /**< ba echo checks */
	BCBUS_UART_CFG_CHECKS_FULL, /**< ba timing shrink 10060 baud, echo checks */
};
/**
 * @brief Modbus serial line parameter
 */
struct bcbus_serial_param {
	/** Baudrate of the serial line */
	uint32_t baud;
	/** parity UART's parity setting:
	 *    UART_CFG_PARITY_NONE,
	 *    UART_CFG_PARITY_EVEN,
	 *    UART_CFG_PARITY_ODD
	 */
	enum uart_config_parity parity;
	/** stop_bits_client UART's stop bits setting if in client mode:
	 *    UART_CFG_STOP_BITS_0_5,
	 *    UART_CFG_STOP_BITS_1,
	 *    UART_CFG_STOP_BITS_1_5,
	 *    UART_CFG_STOP_BITS_2,
	 */
	enum uart_config_stop_bits stop_bits_client;
	enum bcbus_uart_config_ba bcbus_cfg_ba_timing;
};

/**
 * @brief Modbus server parameter
 */
struct bcbus_server_param {
	/** Pointer to the User Callback structure */
	struct bcbus_user_callbacks *user_cb;
	/** Modbus unit ID of the server */
	uint8_t unit_id;
};

struct bcbus_raw_cb {
	bcbus_raw_cb_t raw_tx_cb;
	void *user_data;
};

/**
 * @brief User parameter structure to configure Modbus interface
 *        as client or server.
 */
struct bcbus_iface_param {
	/** Mode of the interface */
	union {
		struct bcbus_server_param server;
		/** Amount of time client will wait for
		 *  a response from the server.
		 */
		struct {
			uint32_t rx_timeout;
			uint32_t ba_echo_timeout;
		};
	};
	union {
		/** Serial support parameter of the interface */
		struct bcbus_serial_param serial;
		/** Pointer to raw ADU callback function */
		struct bcbus_raw_cb rawcb;
	};
};

/**
 * @brief Configure Modbus Interface as raw ADU server
 *
 * @param iface      Modbus RTU interface index
 * @param param      Configuration parameter of the server interface
 *
 * @retval           0 If the function was successful
 */
int bcbus_init_server(const int iface, struct bcbus_iface_param param);

/**
 * @brief Configure Modbus Interface as raw ADU client
 *
 * @param iface      Modbus RTU interface index
 * @param param      Configuration parameter of the client interface
 *
 * @retval           0 If the function was successful
 */
int bcbus_init_client(const int iface, struct bcbus_iface_param param);

/**
 * @brief Disable Modbus Interface
 *
 * This function is called to disable Modbus interface.
 *
 * @param iface      Modbus interface index
 *
 * @retval           0 If the function was successful
 */
int bcbus_disable(const uint8_t iface);

/**
 * @brief Submit raw ADU
 *
 * @param iface      Modbus RTU interface index
 * @param adu        Pointer to the RAW ADU struct that is received
 *
 * @retval           0 If transfer was successful
 */
int bcbus_raw_submit_rx(const int iface, const struct bcbus_adu *adu);

/**
 * @brief Put MBAP header into a buffer
 *
 * @param adu        Pointer to the RAW ADU struct
 * @param header     Pointer to the buffer in which MBAP header
 *                   will be placed.
 */
void bcbus_raw_put_header(const struct bcbus_adu *adu, uint8_t *header);

/**
 * @brief Get MBAP header from a buffer
 *
 * @param adu        Pointer to the RAW ADU struct
 * @param header     Pointer to the buffer containing MBAP header
 */
void bcbus_raw_get_header(struct bcbus_adu *adu, const uint8_t *header);

/**
 * @brief Set Server Device Failure exception
 *
 * This function modifies ADU passed by the pointer.
 *
 * @param adu        Pointer to the RAW ADU struct
 */
void bcbus_raw_set_server_failure(struct bcbus_adu *adu);

/**
 * @brief Use interface as backend to send and receive ADU
 *
 * This function overwrites ADU passed by the pointer and generates
 * exception responses if backend interface is misconfigured or
 * target device is unreachable.
 *
 * @param iface      Modbus client interface index
 * @param adu        Pointer to the RAW ADU struct
 *
 * @retval           0 If transfer was successful
 */
int bcbus_raw_backend_txn(const int iface, struct bcbus_adu *adu);

/**
 * @brief Register a user-defined function code handler.
 *
 * The Modbus specification allows users to define standard function codes
 * missing from Zephyr's Modbus implementation as well as add non-standard
 * function codes in the ranges 65 to 72 and 100 to 110 (decimal), as per
 * specification.
 *
 * This function registers a new handler at runtime for the given
 * function code.
 *
 * @param iface        Modbus client interface index
 * @param custom_fc    User defined function code and callback pair
 *
 * @retval           0 on success
 */
int bcbus_register_user_fc(const int iface, struct bcbus_custom_fc *custom_fc);

#ifdef __cplusplus
}
#endif

/**
 * @}
 */

#endif /* ZEPHYR_INCLUDE_BCBUS_H_ */
