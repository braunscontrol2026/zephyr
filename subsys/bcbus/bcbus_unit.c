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

#include <stdlib.h>
#include <stdio.h>
#include <zephyr/sys/slist.h>
#include <zephyr/bcbus/bcbus_unit.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(bcbus_unit_c, CONFIG_BCBUS_LOG_LEVEL);

#define MAX_NAME_BUSINTERFACE 10

struct _bi_list_node {
	struct busunit unit;
	sys_snode_t _node;
};

struct _bcbus {
	struct bcbus_obj obj;
	sys_slist_t _items;
};

static struct busunit *_new_busunit_ba(uint8_t ba)
{
	struct _bi_list_node *p;

	p = (struct _bi_list_node *)calloc(1, sizeof(struct _bi_list_node));
	if (p != NULL) {
		p->unit.ba = ba;
	} else {
		LOG_ERR("No Memory for Busitem");
	}

	return (struct busunit *)p;
}

static void _delete_busunit(struct busunit *p)
{
	free(p);
}

inline struct busunit *bcbus_item_head(struct bcbus_obj *bus)
{
	struct _bcbus *p = (struct _bcbus *)bus;
	sys_snode_t *th;
	struct _bi_list_node *dummy;

	th = sys_slist_peek_head(&p->_items);

	return (struct busunit *)SYS_SLIST_CONTAINER(th, dummy, _node);
}

inline struct busunit *bcbus_item_tail(struct bcbus_obj *bus)
{
	struct _bcbus *p = (struct _bcbus *)bus;
	sys_snode_t *th;
	struct _bi_list_node *dummy;

	th = sys_slist_peek_tail(&p->_items);

	return (struct busunit *)SYS_SLIST_CONTAINER(th, dummy, _node);
}

inline struct busunit *bcbus_item_next(struct busunit *prev)
{
	sys_snode_t *th;
	struct _bi_list_node *p = (struct _bi_list_node *)prev;
	struct _bi_list_node *dummy;

	th = sys_slist_peek_next(&p->_node);
	return (struct busunit *)SYS_SLIST_CONTAINER(th, dummy, _node);
}

struct busunit *bcbus_add_item(struct bcbus_obj *bus, uint8_t ba)
{
	struct _bcbus *p = (struct _bcbus *)bus;
	struct _bi_list_node *new;

	new = (struct _bi_list_node *)_new_busunit_ba(ba);
	if (new != NULL) {
		sys_slist_append(&p->_items, &new->_node);
	}
	return (struct busunit *)new;
}

struct busunit *bcbus_find_item_ba(struct bcbus_obj *bus, uint8_t ba)
{
	struct busunit *p;

	for (p = bcbus_item_head(bus); p != NULL; p = bcbus_item_next(p)) {
		if (p->ba == ba) {
			return p;
		}
	}
	return NULL;
}

void bcbus_delete_item(struct bcbus_obj *bus, struct busunit *item)
{
	struct _bcbus *pbus;
	struct busunit *prev, *p;
	struct _bi_list_node *i, *j;

	pbus = (struct _bcbus *)bus;
	prev = NULL;
	p = bcbus_item_head(bus);
	while (p != NULL) {
		if (p == item) {
			_delete_busunit(item);
			i = (struct _bi_list_node *)prev;
			j = (struct _bi_list_node *)p;
			sys_slist_remove(&pbus->_items, i != NULL ? &i->_node : NULL, &j->_node);
			return;
		}
		prev = p;
		p = bcbus_item_next(p);
	}
}

void bcbus_cpy_itemdata(struct busunit *dest, const struct busunit *src)
{
	memcpy(dest, src, sizeof(struct busunit));
}

struct bcbus_obj *new_empty_bcbus_obj(const char *name)
{
	struct _bcbus *p;
	char *s;
	size_t len;

	p = (struct _bcbus *)calloc(1, sizeof(struct _bcbus));
	if (p != NULL) {
		len = strlen(name);
		if (len > MAX_NAME_BUSINTERFACE) {
			len = MAX_NAME_BUSINTERFACE;
		}
		s = calloc(1, len + 1);
		if (s != NULL) {
			p->obj.iface_name = strncpy(s, name, len);
		} else {
			LOG_ERR("No Memory for ifacename");
		}
		sys_slist_init(&p->_items);
	} else {
		LOG_ERR("No Memory for Busobject");
	}

	return (struct bcbus_obj *)p;
}

void delete_bcbus_obj(struct bcbus_obj *obj)
{
	struct _bcbus *p = (struct _bcbus *)obj;
	sys_snode_t *tmp;
	struct _bi_list_node *th;

	while ((tmp = sys_slist_get(&p->_items)) != NULL) {
		th = SYS_SLIST_CONTAINER(tmp, th, _node);
		_delete_busunit((struct busunit *)th);
	}
	if (p->obj.iface_name != NULL) {
		free(p->obj.iface_name);
	}

	free(p);
}
