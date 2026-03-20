/*
 * Copyright (c) 2025 Brauns Control GmbH
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 *
 *                   SPDX-License-Identifier: APACHE-2.0
 *
 * This software is subject to an open source license and is distributed by
 *  Brauns Control GmbH pursuant to the terms of the Apache License,
 *      Version 2.0 available at www.apache.org/licenses/LICENSE-2.0.
 */

/**
 * @brief BCBUS unit API
 * @defgroup bcbus BCBUS
 * @ingroup bus items
 * @{
 */

#ifndef ZEPHYR_INCLUDE_BCBUS_UNIT_H_
#define ZEPHYR_INCLUDE_BCBUS_UNIT_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BCBUS_MAPPING 64

/**
 * @brief Frame struct used for Item on bcbus.
 */
struct busunit {
	/** Bus address */
	uint8_t ba;
	void *data;
	/*	uint8_t data[3][BCBUS_MAPPING]; */
	uint32_t poll_cnt;
	uint32_t err_cnt;
};

struct poll_data {
	uint8_t data[3][BCBUS_MAPPING];
};

/**
 * @brief Frame struct used for complete bcbus.
 */
struct bcbus_obj {
	/** Name of Interface */
	char *iface_name;
};

/**
 * @brief Construct an empty bus
 *
 * @param  name    Name of Bus (normally name of interface)
 *
 * @retval pointer to initialized object, NULL if the function fails
 */
struct bcbus_obj *new_empty_bcbus_obj(const char *name);

/**
 * @brief Create and insert a new busitem and get pointer to it
 *
 * @param  bus   pointer to initialized bus
 *
 * @param  ba    Busaddress of new item
 *
 * @retval pointer to new item
 */
struct busunit *bcbus_add_item(struct bcbus_obj *bus, uint8_t ba);
void bcbus_cpy_itemdata(struct busunit *dest, const struct busunit *src);

struct busunit *bcbus_item_head(struct bcbus_obj *bus);
struct busunit *bcbus_item_tail(struct bcbus_obj *bus);
struct busunit *bcbus_item_next(struct busunit *prev);

struct busunit *bcbus_find_item_ba(struct bcbus_obj *bus, uint8_t ba);
void bcbus_delete_item(struct bcbus_obj *bus, struct busunit *item);
/**
 * @brief Destructor: deinitialize an frees a bus
 *
 */
void delete_bcbus_obj(struct bcbus_obj *obj);

int poll_busunit(int iface, struct busunit *unit);

#ifdef __cplusplus
}
#endif

/**
 * @}
 */

#endif /* ZEPHYR_INCLUDE_BCBUS_UNIT_H_ */
