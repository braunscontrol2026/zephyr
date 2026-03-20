/*
 * Copyright (c) 2020 PHYTEC Messtechnik GmbH
 * Copyright (c) 2021 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>
#include <zephyr/drivers/eeprom.h>
#include <zephyr/device.h>
#include <zephyr/random/random.h>
#include <zephyr/modbus/modbus.h>
#include <zephyr/net/socket.h>

#include <zephyr/net/ethernet.h>
#include <zephyr/net/ethernet_mgmt.h>
#include <zephyr/net/net_core.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_context.h>
#include <zephyr/net/net_mgmt.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(tcp_gateway, LOG_LEVEL_INF);

#define MODBUS_TCP_PORT 502
static struct net_mgmt_event_callback mgmt_cb;

#define EEPROM_MAC_OFFSET 0
/*
 * Get a device structure from a devicetree node with alias eeprom-0
 */
static const struct device *get_eeprom_device(void)
{
	const struct device *const dev = DEVICE_DT_GET(DT_ALIAS(mac_eeprom));

	if (!device_is_ready(dev)) {
		printk("\nError: Device \"%s\" is not ready; "
		       "check the driver initialization logs for errors.\n",
		       dev->name);
		return NULL;
	}

	printk("Found EEPROM device \"%s\"\n", dev->name);
	return dev;
}

static int check_generate_mac(struct net_eth_addr *mac)
{
	uint8_t *p = (uint8_t*)mac;

	for(int n=0;n < sizeof(struct net_eth_addr);n++) {
		if(p[n] != 0xff) return 0;
	}

	p[0] = 0x02;
	p[1] = 0x19;
	p[2] = 0x96;

	sys_rand_get(p+3,sizeof(struct net_eth_addr)-3);

	return 1;
}

static void set_mac_addr(struct net_if *iface, void *user_data)
{
	ARG_UNUSED(user_data);
	int rc;
	struct ethernet_req_params values;

	const struct device *eeprom = get_eeprom_device();

	rc = net_if_down(iface);
	if (rc < 0) {
		printk("Error: Couldn't bring network down %d.\n", rc);
		return;
	}

	if (eeprom == NULL) {
		return;
	}

	rc = eeprom_read(eeprom, EEPROM_MAC_OFFSET, &values.mac_address, sizeof(values.mac_address));
	if (rc < 0) {
		printk("Error: Couldn't read eeprom: err: %d.\n", rc);
		return;
	}

	if (check_generate_mac(&values.mac_address))
	{
		rc = eeprom_write(eeprom, EEPROM_MAC_OFFSET, &values.mac_address, sizeof(values.mac_address));
		if (rc < 0) {
			printk("Error: Couldn't write eeprom: err: %d.\n", rc);
		}
	}

	rc = net_mgmt(NET_REQUEST_ETHERNET_SET_MAC_ADDRESS, iface,
		       &values, sizeof(struct ethernet_req_params));

	LOG_INF("Set MAC on %s: index=%d", net_if_get_device(iface)->name,
		net_if_get_by_iface(iface));

	rc = net_if_up(iface);

	if (rc < 0) {
		printk("Error: Couldn't bring network up %d.\n", rc);
		return;
	}




}

static void start_dhcpv4_client(struct net_if *iface, void *user_data)
{
	ARG_UNUSED(user_data);

	LOG_INF("Start on %s: index=%d", net_if_get_device(iface)->name,
		net_if_get_by_iface(iface));
	net_dhcpv4_start(iface);
}

static struct modbus_adu tmp_adu;
static int backend;

const static struct modbus_iface_param backend_param = {
	.mode = MODBUS_MODE_RTU,
	.rx_timeout = 800000,
	.serial = {
		.baud = 19200,
		.parity = UART_CFG_PARITY_EVEN,
		.stop_bits_client = UART_CFG_STOP_BITS_1,
	},
};

#define MODBUS_NODE DT_COMPAT_GET_ANY_STATUS_OKAY(zephyr_modbus_serial)

static int init_backend_iface(void)
{
	const char bend_name[] = {DEVICE_DT_NAME(MODBUS_NODE)};

	backend = modbus_iface_get_by_name(bend_name);
	if (backend < 0) {
		LOG_ERR("Failed to get iface index for %s",
			bend_name);
		return -ENODEV;
	}

	return modbus_init_client(backend, backend_param);
}

static int modbus_tcp_reply(int client, struct modbus_adu *adu)
{
	uint8_t header[MODBUS_MBAP_AND_FC_LENGTH];

	modbus_raw_put_header(adu, header);
	if (send(client, header, sizeof(header), 0) < 0) {
		return -errno;
	}

	if (send(client, adu->data, adu->length, 0) < 0) {
		return -errno;
	}

	return 0;
}

static int modbus_tcp_connection(int client)
{
	uint8_t header[MODBUS_MBAP_AND_FC_LENGTH];
	int rc;
	int data_len;

	rc = recv(client, header, sizeof(header), MSG_WAITALL);
	if (rc <= 0) {
		return rc == 0 ? -ENOTCONN : -errno;
	}

	LOG_HEXDUMP_DBG(header, sizeof(header), "h:>");
	modbus_raw_get_header(&tmp_adu, header);
	data_len = tmp_adu.length;

	rc = recv(client, tmp_adu.data, data_len, MSG_WAITALL);
	if (rc <= 0) {
		return rc == 0 ? -ENOTCONN : -errno;
	}

	LOG_HEXDUMP_DBG(tmp_adu.data, tmp_adu.length, "d:>");
	rc = modbus_raw_backend_txn(backend, &tmp_adu);
	if (rc == -ENOTSUP || rc == -ENODEV) {
		LOG_WRN("Backend interface error: %d", rc);
	}

	return modbus_tcp_reply(client, &tmp_adu);
}

static void handler(struct net_mgmt_event_callback *cb,
		    uint64_t mgmt_event,
		    struct net_if *iface)
{
	int i = 0;

	if (mgmt_event != NET_EVENT_IPV4_ADDR_ADD) {
		return;
	}

	for (i = 0; i < NET_IF_MAX_IPV4_ADDR; i++) {
		char buf[NET_IPV4_ADDR_LEN];

		if (iface->config.ip.ipv4->unicast[i].ipv4.addr_type !=
							NET_ADDR_DHCP) {
			continue;
		}

		LOG_INF("   Address[%d]: %s", net_if_get_by_iface(iface),
			net_addr_ntop(AF_INET,
			    &iface->config.ip.ipv4->unicast[i].ipv4.address.in_addr,
						  buf, sizeof(buf)));
		LOG_INF("    Subnet[%d]: %s", net_if_get_by_iface(iface),
			net_addr_ntop(AF_INET,
				       &iface->config.ip.ipv4->unicast[i].netmask,
				       buf, sizeof(buf)));
		LOG_INF("    Router[%d]: %s", net_if_get_by_iface(iface),
			net_addr_ntop(AF_INET,
						 &iface->config.ip.ipv4->gw,
						 buf, sizeof(buf)));
		LOG_INF("Lease time[%d]: %u seconds", net_if_get_by_iface(iface),
			iface->config.dhcpv4.lease_time);
	}
}

int main(void)
{
	int serv;
	struct sockaddr_in bind_addr;
	static int counter;

	LOG_INF("Set MAC addr");

	net_if_foreach(set_mac_addr, NULL);

	LOG_INF("Run dhcpv4 client");

	net_mgmt_init_event_callback(&mgmt_cb, (net_mgmt_event_handler_t) handler,
				     NET_EVENT_IPV4_ADDR_ADD);
	net_mgmt_add_event_callback(&mgmt_cb);

	net_if_foreach(start_dhcpv4_client, NULL);

	if (init_backend_iface()) {
		LOG_ERR("Modbus initialization failed");
		return 0;
	}

	serv = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

	if (serv < 0) {
		LOG_ERR("error: socket: %d", errno);
		return 0;
	}

	bind_addr.sin_family = AF_INET;
	bind_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	bind_addr.sin_port = htons(MODBUS_TCP_PORT);

	if (bind(serv, (struct sockaddr *)&bind_addr, sizeof(bind_addr)) < 0) {
		LOG_ERR("error: bind: %d", errno);
		return 0;
	}

	if (listen(serv, 5) < 0) {
		LOG_ERR("error: listen: %d", errno);
		return 0;
	}

	LOG_INF("Started MODBUS TCP gateway example on port %d", MODBUS_TCP_PORT);

	while (1) {
		struct sockaddr_in client_addr;
		socklen_t client_addr_len = sizeof(client_addr);
		char addr_str[INET_ADDRSTRLEN];
		int client;
		int rc;

		client = accept(serv, (struct sockaddr *)&client_addr,
				&client_addr_len);

		if (client < 0) {
			LOG_ERR("error: accept: %d", errno);
			continue;
		}

		inet_ntop(client_addr.sin_family, &client_addr.sin_addr,
			  addr_str, sizeof(addr_str));
		LOG_INF("Connection #%d from %s",
			counter++, addr_str);

		do {
			rc = modbus_tcp_connection(client);
		} while (!rc);

		close(client);
		LOG_INF("Connection from %s closed, errno %d",
			addr_str, rc);
	}
	return 0;
}
