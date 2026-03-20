/*
 * Copyright (c) 2017 Intel Corporation.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdbool.h>
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(app_gateway, LOG_LEVEL_DBG);

#include <zephyr/kernel.h>
#include <zephyr/drivers/eeprom.h>
#include <zephyr/drivers/watchdog.h>
#include <zephyr/shell/shell.h>
#include <zephyr/shell/shell_uart.h>
#include <zephyr/version.h>
#include <zephyr/linker/sections.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef CONFIG_ARCH_POSIX
#include <unistd.h>
#else
#include <zephyr/posix/unistd.h>
#endif
#include <zephyr/fs/fs.h>
#include <zephyr/fs/littlefs.h>
#include <ff.h>

#include <zephyr/net/ethernet.h>
#include <zephyr/net/ethernet_mgmt.h>
#include <zephyr/random/random.h>
#include <zephyr/net/net_core.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_context.h>
#include <zephyr/net/net_mgmt.h>

#include "net_sample_common.h"

#include <zephyr/sys/ring_buffer.h>

#include <zephyr/bcbus/bcbus.h>
#include <zephyr/bcbus/bcbus_unit.h>

#include <zephyr/drivers/pwm.h>

#include "cJSON.h"
#include "sendfile.h"
#include "http_server.h"
#include "polling.h"
#ifdef CONFIG_QSPI
#include "psram.h"
#endif

static bool silent = false;
static char echo_char;

#define PR_SHELL(sh, fmt, ...)                                                                     \
	if (!silent)                                                                               \
	shell_fprintf(sh, SHELL_NORMAL, fmt, ##__VA_ARGS__)
#define PR_ERROR(sh, fmt, ...) shell_fprintf(sh, SHELL_ERROR, fmt, ##__VA_ARGS__)
#define SHELL_EXECUTE_SILENT(sh, ...)                                                              \
	silent = true;                                                                             \
	shell_execute_cmd(sh, ##__VA_ARGS__);                                                      \
	silent = false;

RING_BUF_DECLARE(forth_key_pipe, 100);
RING_BUF_DECLARE(forth_emit_pipe, 300);

/* scheduling priority used by each thread */
#define PRIORITY 14

const struct shell *shp = NULL;

#define COUNT_BUS 4

struct _linebuf {
	char linebuf[CONFIG_BCBUS_LINE_SIZE];
	uint8_t lb_ptr;
	char *line;
};

struct _bc_bus_stats {
	uint8_t client_ba;
	const struct shell *sh;
	struct _linebuf line_in;
	struct _linebuf line_out;
	struct sendfile_job *fjob;
};

static struct _bc_bus_stats bc_bus_stats[COUNT_BUS];

int wdt_channel_id;
const struct device *const wdt = DEVICE_DT_GET(DT_ALIAS(watchdog0));
bool wdg_trigger = true;

#ifndef WDT_MAX_WINDOW
#define WDT_MAX_WINDOW 10000U
#endif

#ifndef WDT_MIN_WINDOW
#define WDT_MIN_WINDOW 0U
#endif

static const struct pwm_dt_spec red_pwm_led = PWM_DT_SPEC_GET(DT_ALIAS(red_pwm_led));
static const struct pwm_dt_spec green_pwm_led = PWM_DT_SPEC_GET(DT_ALIAS(green_pwm_led));
static const struct pwm_dt_spec blue_pwm_led = PWM_DT_SPEC_GET(DT_ALIAS(blue_pwm_led));

#define STEP_SIZE PWM_USEC(2000)

static void pulse_led(void)
{
	static uint32_t pulse_red, pulse_green, pulse_blue; /* pulse widths */

	if (pulse_red <= red_pwm_led.period) {
		pulse_red += STEP_SIZE;

		pwm_set_pulse_dt(&red_pwm_led, pulse_red);
	} else if (pulse_green <= green_pwm_led.period) {
		pulse_green += STEP_SIZE;

		pwm_set_pulse_dt(&green_pwm_led, pulse_green);

	} else if (pulse_blue <= blue_pwm_led.period) {
		pulse_blue += STEP_SIZE;
		pwm_set_pulse_dt(&blue_pwm_led, pulse_blue);
	} else {
		pulse_red = 0U;
		pulse_green = 0U;
		pulse_blue = 0U;
	}
}

static int bcbus_get_iface_by_shell(const struct shell *sh)
{
	for (int i = 0; i < COUNT_BUS; i++) {
		if (bc_bus_stats[i].sh == sh) {
			return i;
		}
	}

	return -ENODEV;
}

static int bcbus_linebuf(struct _linebuf *lb, uint8_t *data, size_t len)
{
	char c;
	int bytes_proc = 0;
	int pos;

	pos = lb->lb_ptr;
	while (bytes_proc < len) {
		c = data[bytes_proc++];
		if (c != '\n') {
			lb->linebuf[pos] = c;
			if ((pos > sizeof(lb->linebuf) - 2) || (c == '\r')) {
				lb->linebuf[pos + 1] = '\0';
				if (lb->line != NULL) {
					free(lb->line);
				}
				lb->line = malloc(strlen(lb->linebuf) + 1);
				if (lb->line != NULL) {
					strcpy(lb->line, lb->linebuf);
					pos = 0;
				} else {
					LOG_ERR("linebuf no memory");
				}

			} else if (c == '\b' || c == 127) {
				--pos;
			} else {
				++pos;
			}
		}
	}
	lb->lb_ptr = pos;
	return bytes_proc;
}

/* called from terminal.s */
void mecrisp_emit(uint8_t c)
{
	int n;
	for (n = 0; ring_buf_put(&forth_emit_pipe, &c, 1) != 1; n++) {
		k_sleep(K_MSEC(1));
		if (n > 100) {
			break;
		}
	}
}

/* called from terminal.s */
int32_t mecrisp_qkey(void)
{
	k_sleep(K_MSEC(5));
	return ring_buf_is_empty(&forth_key_pipe) == 0;
}

/* called from terminal.s */
int32_t mecrisp_qemit(void)
{
	k_sleep(K_MSEC(5));
	return ring_buf_space_get(&forth_emit_pipe) != 0;
}

/* called from terminal.s */
int32_t mecrisp_key(void)
{
	uint8_t c;

	while (ring_buf_get(&forth_key_pipe, &c, 1) != 1) {
		k_sleep(K_MSEC(10));
	}
	return c;
}

static void line_out_forth(void)
{
	uint8_t s[100];
	int rc;

	rc = ring_buf_get(&forth_emit_pipe, &s[0], 100);
	if (rc > 0) {
		s[rc] = 0;
		PR_SHELL(shp, "%s", s);
	}
}

static void line_request_filemode(struct _bc_bus_stats *bs)
{
	int iface, len, nput;
	char *line;
	iface = bcbus_get_iface_by_shell(bs->sh);

	line = bs->fjob->line_to_send;

	if (line != NULL) {
		len = strlen(line);
		nput = 0;
		while (len > nput) {
			nput += bcbus_tty_client_put(iface, bs->client_ba, &line[nput], len - nput);
			bcbus_tty_client_poll(iface);
		}
		bs->fjob->send_line = bs->fjob->line_to_send;
		bs->fjob->line_to_send = NULL;
	}
}

static void line_out_tty_mode(struct _bc_bus_stats *bs)
{
	int rc, n, read_pos;
	uint8_t s[100];

	n = bcbus_get_iface_by_shell(bs->sh);
	if (bs->client_ba != 0 && bs->sh != NULL) {
		if (echo_char) {
			PR_SHELL(bs->sh, "%c", echo_char);
			echo_char = 0;
		}
		read_pos = 0;
		rc = bcbus_tty_client_get(n, bs->client_ba, &s[0], 100);
		while (rc > read_pos) {
			read_pos += bcbus_linebuf(&bs->line_out, &s[read_pos], rc - read_pos);

			if (bs->line_out.line != NULL) {
				PR_SHELL(bs->sh, "%s\n", bs->line_out.line);

				if (bs->fjob != NULL) {
					bs->fjob->response_line = bs->line_out.line;
					bcbus_sendfile_check_response(bs->fjob);
				}
				if (wdg_trigger) {
					wdt_feed(wdt, wdt_channel_id);
				}

				free(bs->line_out.line);
				bs->line_out.line = NULL;
			}
		}
	}

	if (bs->fjob != NULL) {
		line_request_filemode(bs);
	}
}

static int cmd_pvis_time(const struct shell *sh, size_t argc, char **argv)
{
	set_pvis_time(atoi(argv[1]));
	PR_SHELL(sh, "--- PVis Time Out %d ---\n", atoi(argv[1]));
	return 0;
}

void cmd_wdog_reset(const struct shell *sh, size_t argc, char **argv)
{
	wdg_trigger = false;
}

static void print_sys_memory_stats(const struct shell *sh, struct sys_heap *hp)
{
	struct sys_memory_stats stats;

	sys_heap_runtime_stats_get(hp, &stats);

	PR_SHELL(sh, "allocated %zu, free %zu, max allocated %zu\n", stats.allocated_bytes,
		 stats.free_bytes, stats.max_allocated_bytes);
}

static void print_static_heaps(const struct shell *sh)
{
	struct k_heap *ha;
	int n;

	n = k_heap_array_get(&ha);
	PR_SHELL(sh, "%d static heap(s) allocated:\n", n);

	for (int i = 0; i < n; i++) {
		PR_SHELL(sh, "\t%d - address %p ", i, &ha[i]);
		print_sys_memory_stats(sh, &ha[i].heap);
	}
}

static void print_all_heaps(const struct shell *sh)
{
	struct sys_heap **ha;
	int n;

	n = sys_heap_array_get(&ha);
	PR_SHELL(sh, "%d heap(s) allocated (including static):\n", n);

	for (int i = 0; i < n; i++) {
		PR_SHELL(sh, "\t%d - address %p ", i, ha[i]);
		print_sys_memory_stats(sh, ha[i]);
	}
}
void cmd_memstat(const struct shell *sh, size_t argc, char **argv)
{
	print_static_heaps(sh);
	print_all_heaps(sh);
}

void thread_poll_emit_buf_entry(void *dummy1, void *dummy2, void *dummy3)
{
	ARG_UNUSED(dummy1);
	ARG_UNUSED(dummy2);
	ARG_UNUSED(dummy3);
	int n, err;
	struct _bc_bus_stats *bs;

	err = wdt_setup(wdt, 0);
	if (err < 0) {
		printk("Watchdog setup error\n");
	}

	while (1) {
		if (shp != NULL) {
			line_out_forth();
		}

		for (n = 0; n < COUNT_BUS; n++) {
			bs = &bc_bus_stats[n];
			line_out_tty_mode(bs);
		}

		k_sleep(K_MSEC(10));
		if (wdg_trigger) {
			wdt_feed(wdt, wdt_channel_id);
		}
		pulse_led();
	}
}

/* called from terminal.s */
void stop_shell_bypass_forth(void)
{
	if (shp != NULL) {
		shell_set_bypass(shp, NULL);
		PR_SHELL(shp, "\n--- shell ---\n");
		shp = NULL;
		k_sleep(K_MSEC(1000));
		ring_buf_reset(&forth_emit_pipe);
	}
}

static void bypass_cb(const struct shell *sh, uint8_t *keydata, size_t len)
{
	int rc;
	size_t bytes_written = 0;

	int n;
	for (n = 0; n < 100; ++n) {
		rc = ring_buf_put(&forth_key_pipe, &keydata[bytes_written], len - bytes_written);
		bytes_written += rc;
		if (bytes_written >= len) {
			break;
		}
		k_sleep(K_MSEC(1));
	}
	if (bytes_written != len) {
		PR_ERROR(sh, "Forth Core no response ! - exit forth cli\n");
		stop_shell_bypass_forth();
	}
}

extern void mecrisp_core(void);

void mecrisp_entry(void *dummy1, void *dummy2, void *dummy3)
{
	ARG_UNUSED(dummy1);
	ARG_UNUSED(dummy2);
	ARG_UNUSED(dummy3);
	k_sleep(K_SECONDS(10));
	mecrisp_core();
}

K_THREAD_DEFINE(thread_forth, 320, mecrisp_entry, NULL, NULL, NULL, PRIORITY, 0, 10);
K_THREAD_DEFINE(thread_poll, 2048, thread_poll_emit_buf_entry, NULL, NULL, NULL, PRIORITY - 1, 0,
		0);

static int cmd_forth(const struct shell *sh, size_t argc, char **argv)
{
	shell_set_bypass(sh, bypass_cb);
	/* start shell print thread here */
	shp = sh;
	PR_SHELL(shp, "\n--- forth ---\n");

	return 0;
}

#define EEPROM_MAC_OFFSET 0
#define EEPROM_TFTP_ADR   (EEPROM_MAC_OFFSET + NET_ETH_ADDR_LEN)
/*
 * Get a device structure from a devicetree node with alias eeprom-0
 */
static const struct device *get_eeprom_device(void)
{
	const struct device *const dev = DEVICE_DT_GET(DT_ALIAS(mac_eeprom));

	if (!device_is_ready(dev)) {
		LOG_ERR("Device \"%s\" is not ready "
			"check the driver initialization logs for errors.\n",
			dev->name);
		return NULL;
	}

	return dev;
}

static int check_generate_mac(struct net_eth_addr *mac)
{
	uint8_t *p = (uint8_t *)mac;

	for (int n = 0; n < sizeof(struct net_eth_addr); n++) {
		if (p[n] != 0xff) {
			return 0;
		}
	}

	p[0] = 0x02;
	p[1] = 0x19;
	p[2] = 0x96;

	sys_rand_get(p + 3, sizeof(struct net_eth_addr) - 3);

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

	rc = eeprom_read(eeprom, EEPROM_MAC_OFFSET, &values.mac_address,
			 sizeof(values.mac_address));
	if (rc < 0) {
		printk("Error: Couldn't read eeprom: err: %d.\n", rc);
		return;
	}

	if (check_generate_mac(&values.mac_address)) {
		rc = eeprom_write(eeprom, EEPROM_MAC_OFFSET, &values.mac_address,
				  sizeof(values.mac_address));
		if (rc < 0) {
			printk("Error: Couldn't write eeprom: err: %d.\n", rc);
		}
	}

	rc = net_mgmt(NET_REQUEST_ETHERNET_SET_MAC_ADDRESS, iface, &values,
		      sizeof(struct ethernet_req_params));

	LOG_INF("Set MAC on %s: index=%d", net_if_get_device(iface)->name,
		net_if_get_by_iface(iface));

	rc = net_if_up(iface);

	if (rc < 0) {
		printk("Error: Couldn't bring network up %d.\n", rc);
		return;
	}
}

#define DHCP_OPTION_NTP (42)

static char ntp_server[NET_IPV4_ADDR_LEN];

static struct net_mgmt_event_callback mgmt_cb;

static struct net_dhcpv4_option_callback dhcp_cb;

static void start_dhcpv4_client(struct net_if *iface, void *user_data)
{
	ARG_UNUSED(user_data);

	LOG_INF("Start on %s: index=%d", net_if_get_device(iface)->name,
		net_if_get_by_iface(iface));
	net_dhcpv4_start(iface);
}

static void net_cb_handler(struct net_mgmt_event_callback *cb, uint64_t mgmt_event,
			   struct net_if *iface)
{
	int i = 0;

	if (mgmt_event != NET_EVENT_IPV4_ADDR_ADD) {
		return;
	}

	for (i = 0; i < NET_IF_MAX_IPV4_ADDR; i++) {
		char buf[NET_IPV4_ADDR_LEN];

		if (iface->config.ip.ipv4->unicast[i].ipv4.addr_type != NET_ADDR_DHCP) {
			continue;
		}

		LOG_INF("   Address[%d]: %s", net_if_get_by_iface(iface),
			net_addr_ntop(AF_INET,
				      &iface->config.ip.ipv4->unicast[i].ipv4.address.in_addr, buf,
				      sizeof(buf)));
		LOG_INF("    Subnet[%d]: %s", net_if_get_by_iface(iface),
			net_addr_ntop(AF_INET, &iface->config.ip.ipv4->unicast[i].netmask, buf,
				      sizeof(buf)));
		LOG_INF("    Router[%d]: %s", net_if_get_by_iface(iface),
			net_addr_ntop(AF_INET, &iface->config.ip.ipv4->gw, buf, sizeof(buf)));
		LOG_INF("Lease time[%d]: %u seconds", net_if_get_by_iface(iface),
			iface->config.dhcpv4.lease_time);
	}
}

static void option_handler(struct net_dhcpv4_option_callback *cb, size_t length,
			   enum net_dhcpv4_msg_type msg_type, struct net_if *iface)
{
	char buf[NET_IPV4_ADDR_LEN];

	LOG_INF("DHCP Option %d: %s", cb->option,
		net_addr_ntop(AF_INET, cb->data, buf, sizeof(buf)));
}

/* BUS BUS BUS */

static const struct bcbus_iface_param client_param = {
	.rx_timeout = 250000,
	.ba_echo_timeout = 20000,
	.serial =
		{
			.baud = 9600,
			.parity = UART_CFG_PARITY_NONE,
			.stop_bits_client = UART_CFG_STOP_BITS_1,
			.bcbus_cfg_ba_timing = BCBUS_UART_CFG_BA_SHRINK,
		},
};

static int init_bcbus_clients(void)
{
	int n, m, rc;
	struct _bc_bus_stats *bs;

	for (n = 0, rc = 0; n < COUNT_BUS; n++) {
		bs = &bc_bus_stats[n];
		bs->sh = NULL;
		bs->client_ba = 0;
		m = bcbus_init_client(n, client_param);
		rc = MIN(rc, m);
	}

	return rc;
}

static struct bcbus_obj *create_bcbus_config(const struct shell *sh, const char *busname,
					     const char *str)
{
	struct bcbus_obj *bus;
	const cJSON *iface = NULL;
	const cJSON *item = NULL;
	const cJSON *items = NULL;

	bus = new_empty_bcbus_obj(busname);
	cJSON *bus_json = cJSON_Parse(str);
	if (bus_json == NULL) {
		const char *error_ptr = cJSON_GetErrorPtr();
		if (error_ptr != NULL) {
			PR_ERROR(sh, "Error before: %s\n", error_ptr);
		}
	}
	iface = cJSON_GetObjectItem(bus_json, "iface");
	if (!cJSON_IsString(iface) && (iface->valuestring == NULL)) {
		PR_ERROR(sh, "No iface found!\n");
	}
	if (strcmp(iface->valuestring, busname) != 0) {
		PR_ERROR(sh, "expect:%s found:%s:\n", busname, iface->valuestring);
	}
	items = cJSON_GetObjectItem(bus_json, "items");
	cJSON_ArrayForEach(item, items)
	{
		cJSON *ba_json = cJSON_GetObjectItem(item, "ba");
		bcbus_add_item(bus, ba_json->valueint);
	}

	return bus;
}

static char *create_config_string(struct bcbus_obj *bus)
{
	cJSON *busobj = NULL;
	char *string = NULL;
	struct busunit *p;

	cJSON *config = cJSON_CreateObject();

	if (cJSON_AddStringToObject(config, "iface", bus->iface_name) == NULL) {
		goto end;
	}

	busobj = cJSON_AddArrayToObject(config, "items");
	if (busobj == NULL) {
		goto end;
	}

	for (p = bcbus_item_head(bus); p != NULL; p = bcbus_item_next(p)) {
		cJSON *busadr = cJSON_CreateObject();
		if (cJSON_AddNumberToObject(busadr, "ba", p->ba) == NULL) {
			goto end;
		}
		cJSON_AddItemToArray(busobj, busadr);
	}

	string = cJSON_PrintUnformatted(config);

end:
	cJSON_Delete(config);
	return string;
}

static void print_hexdump(const struct shell *sh, uint8_t *data, uint8_t len)
{
	int i;
	for (i = 0; i < len; i++) {
		PR_SHELL(sh, "%02x ", data[i]);
	}
}

static const char *get_busname_idx(uint8_t n)
{
	static char client_name[] = "bcbus0";
	client_name[5] = '0' + MIN(COUNT_BUS, n);

	return client_name;
}

static int cmd_config_load_bus(const struct shell *sh, size_t argc, char **argv)
{
	int busidx, rc = 0;
	uint8_t client_iface;
	const char *busname;
	char *cstr = NULL;
	char *fname = NULL;

	struct bcbus_obj *tmp_bus;
	struct fs_file_t file;
	struct fs_dirent dirent;
	const char path_name[] = "/lfs/etc/";

	if (argc == 2) {
		busidx = atoi(argv[1]);
		busname = get_busname_idx(busidx);
		client_iface = bcbus_iface_get_by_name(busname);
		PR_SHELL(sh, "--- Bus load config %s ---\n", busname);

		fs_file_t_init(&file);
		fname = calloc(1, strlen(path_name) + strlen(busname) + 1);
		if (fname == NULL) {
			PR_ERROR(sh, "no memory!");
			rc = -ENOMEM;
			goto cmd_config_load_bus_ex;
		}
		strcpy(fname, path_name);
		strcat(fname, busname);
		fs_stat(fname, &dirent);
		rc = fs_open(&file, fname, FS_O_READ);
		if (rc < 0) {
			PR_ERROR(sh, "FAIL: open %s: %d", fname, rc);
			goto cmd_config_load_bus_ex;
		}
		cstr = malloc(dirent.size + 1);
		if (cstr == NULL) {
			PR_ERROR(sh, "no memory!");
			rc = -ENOMEM;
			goto cmd_config_load_bus_ex;
		}
		rc = fs_read(&file, cstr, dirent.size);
		fs_close(&file);
		if (rc < 0) {
			PR_ERROR(sh, "FAIL: read %s: %d", fname, rc);
			goto cmd_config_load_bus_ex;
		}
		cstr[rc] = '\0';
		tmp_bus = create_bcbus_config(sh, busname, cstr);
		if (bcbus_replace_poll_list(client_iface, tmp_bus) != 0) {
			delete_bcbus_obj(tmp_bus);
		}
	}
cmd_config_load_bus_ex:
	if (fname != NULL) {
		free(fname);
	}
	if (cstr != NULL) {
		free(cstr);
	}
	return rc;
}

static int cmd_config_save_bus(const struct shell *sh, size_t argc, char **argv)
{
	int busidx, rc = 0;
	uint8_t client_iface;
	const char *busname;
	char *cstr = NULL;
	char *fname = NULL;

	struct bcbus_obj *tmp_bus;
	struct fs_file_t file;
	const char path_name[] = "/lfs/etc/";

	if (argc == 2) {
		busidx = atoi(argv[1]);
		busname = get_busname_idx(busidx);
		client_iface = bcbus_iface_get_by_name(busname);
		PR_SHELL(sh, "--- Bus save config %s ---\n", busname);
		tmp_bus = bcbus_get_copy_poll_list(client_iface);
		cstr = create_config_string(tmp_bus);
		delete_bcbus_obj(tmp_bus);
		if (cstr != NULL) {
			fs_file_t_init(&file);
			fname = calloc(1, strlen(path_name) + strlen(busname) + 1);
			if (fname == NULL) {
				PR_ERROR(sh, "no memory!");
				rc = -ENOMEM;
				goto cmd_config_save_bus_ex;
			}
			strcpy(fname, path_name);
			strcat(fname, busname);
			rc = fs_open(&file, fname, FS_O_CREATE | FS_O_RDWR);
			if (rc < 0) {
				PR_ERROR(sh, "FAIL: open %s: %d", fname, rc);
				goto cmd_config_save_bus_ex;
			}
			rc = fs_write(&file, cstr, strlen(cstr));
			fs_close(&file);
			if (rc < 0) {
				PR_ERROR(sh, "FAIL: write %s: %d", fname, rc);
				goto cmd_config_save_bus_ex;
			}
		}
	}
cmd_config_save_bus_ex:
	if (fname != NULL) {
		free(fname);
	}
	if (cstr != NULL) {
		free(cstr);
	}
	return rc;
}

static int cmd_delete_ba(const struct shell *sh, size_t argc, char **argv)
{
	int busidx;
	uint8_t client_iface, ba;
	const char *busname;
	struct bcbus_obj *tmp_bus;
	struct busunit *p;

	if (argc == 3) {
		busidx = atoi(argv[1]);
		busname = get_busname_idx(busidx);
		client_iface = bcbus_iface_get_by_name(busname);
		ba = atoi(argv[2]);
		PR_SHELL(sh, "--- BA delete %s->%d --- ", busname, ba);
		tmp_bus = bcbus_get_copy_poll_list(client_iface);
		stop_polling(client_iface);
		p = bcbus_find_item_ba(tmp_bus, ba);
		if (p == NULL) {
			PR_SHELL(sh, "not found!\n");
		} else {
			bcbus_delete_item(tmp_bus, p);
			PR_SHELL(sh, "deleted!\n");
		}

		if (bcbus_replace_poll_list(client_iface, tmp_bus) != 0) {
			delete_bcbus_obj(tmp_bus);
		}
	}
	return 0;
}

static int cmd_list_bus(const struct shell *sh, size_t argc, char **argv)
{
	int busidx;
	uint8_t client_iface;
	const char *busname;
	struct bcbus_obj *tmp_bus;
	struct busunit *p;
	int res = 0;
	char s_buf[20];

	struct bcbus_tele bustele = {
		.txbuf = {0, 1, SEND_SER_NUM},
	};

	if (argc == 2) {
		busidx = atoi(argv[1]);
		busname = get_busname_idx(busidx);
		client_iface = bcbus_iface_get_by_name(busname);
		PR_SHELL(sh, "--- List %s ---", busname);
		tmp_bus = bcbus_get_copy_poll_list(client_iface);
		for (p = bcbus_item_head(tmp_bus); p != NULL; p = bcbus_item_next(p)) {
			PR_SHELL(sh, "\n  %d->", p->ba);
			res = bcbus_do_client_handshake(client_iface, p->ba, &bustele);
			if (res == 0) {
				print_hexdump(sh, &bustele.rxbuf[2], bustele.rxbuf[1]);
			}
			PR_SHELL(sh, " %s",
				 get_poll_stats(s_buf, sizeof(s_buf), client_iface, p->ba));
		}
		PR_SHELL(sh, "\n");
		delete_bcbus_obj(tmp_bus);
	}
	return res;
}

static int cmd_scan_bus(const struct shell *sh, size_t argc, char **argv)
{
	int busidx;
	int start, end, n;
	int res = -1;
	uint8_t client_iface;
	const char *busname;
	struct bcbus_obj *tmp_bus;
	struct bcbus_tele bustele = {
		.txbuf = {0, 1, SEND_SER_NUM},
	};

	if (argc == 4) {
		busidx = atoi(argv[1]);
		start = atoi(argv[2]);
		end = atoi(argv[3]);
		busname = get_busname_idx(busidx);
		client_iface = bcbus_iface_get_by_name(busname);
		stop_polling(client_iface);
		tmp_bus = bcbus_get_copy_poll_list(client_iface);
		PR_SHELL(sh, "--- Scan %s ---\n", busname);
		if (start > end) {
			n = end;
			end = start;
			start = n;
		}
		for (n = start; n <= end; n++) {
			res = bcbus_do_client_handshake(client_iface, n, &bustele);
			if (res == 0) {
				PR_SHELL(sh, ">%d", n);
				if (bcbus_find_item_ba(tmp_bus, n) == NULL) {
					bcbus_add_item(tmp_bus, n);
				}
			} else {
				PR_SHELL(sh, "-");
			}
		}
		PR_SHELL(sh, "\n");

		if (bcbus_replace_poll_list(client_iface, tmp_bus) != 0) {
			delete_bcbus_obj(tmp_bus);
		}
		res = 0;
	}
	return res;
}

static int cmd_poll_snr(const struct shell *sh, size_t argc, char **argv)
{
	int client_iface;
	uint8_t client_ba;
	int res;

	struct bcbus_tele bustele = {
		.txbuf = {0, 1, SEND_SER_NUM},
	};

	if (argc == 3) {
		client_iface = bcbus_iface_get_by_name(get_busname_idx(atoi(argv[1])));
		client_ba = atoi(argv[2]);
		PR_SHELL(sh, "--- Poll SNR ---\n");
		res = bcbus_do_client_handshake(client_iface, client_ba, &bustele);
		if (res == 0) {
			print_hexdump(sh, &bustele.rxbuf[2], bustele.rxbuf[1]);
			PR_SHELL(sh, "\n");
		}

	} else {
		PR_SHELL(sh, "wrong argument count\n"
			     "Poll BCbus [bus] [ba]\n");
		res = 0;
	}

	return res;
}

static int cmd_poll_start(const struct shell *sh, size_t argc, char **argv)
{
	int client_iface;
	const char *busname;
	int res;

	if (argc == 2) {
		busname = get_busname_idx(atoi(argv[1]));
		client_iface = bcbus_iface_get_by_name(busname);
		PR_SHELL(sh, "--- Poll Start Bus %s ---\n", busname);
		start_polling(client_iface);

	} else {
		PR_SHELL(sh, "wrong argument count\n"
			     "Poll BCbus [bus]\n");
		res = 0;
	}

	return res;
}

static int cmd_poll_stop(const struct shell *sh, size_t argc, char **argv)
{
	int client_iface;
	const char *busname;
	int res;

	if (argc == 2) {
		busname = get_busname_idx(atoi(argv[1]));
		client_iface = bcbus_iface_get_by_name(busname);
		PR_SHELL(sh, "--- Poll Stop Bus %s ---\n", busname);
		stop_polling(client_iface);

	} else {
		PR_SHELL(sh, "wrong argument count\n"
			     "Poll BCbus [bus]\n");
		res = 0;
	}

	return res;
}

static int cmd_bus_status(const struct shell *sh, size_t argc, char **argv)
{
	int client_iface;
	const char *busname;
	int res;

	if (argc == 2) {
		busname = get_busname_idx(atoi(argv[1]));
		client_iface = bcbus_iface_get_by_name(busname);
		PR_SHELL(sh, "--- Poll Status Bus %s %s ---\n", busname,
			 poll_state_txt(status_polling(client_iface)));
	} else {
		PR_SHELL(sh, "wrong argument count\n"
			     "Poll BCbus [bus]\n");
		res = 0;
	}

	return res;
}

static void bcbus_stop_shell_bypass(const struct shell *sh)
{
	int iface = bcbus_get_iface_by_shell(sh);

	if (iface < 0) {
		return;
	}

	memset(&bc_bus_stats[iface], 0, sizeof(struct _bc_bus_stats));

	shell_set_bypass(sh, NULL);
}

static void bcbus_send_debrick(const int iface, uint8_t ba)
{
	struct bcbus_tele bustele = {
		.txbuf = {0, 2, PROC_FLASH, 0x55},
	};

	bcbus_do_client_handshake(iface, ba, &bustele);
	k_sleep(K_MSEC(10));
	bustele.txbuf[3] = 0xAA;
	bcbus_do_client_handshake(iface, ba, &bustele);
}

const char *const bpath = "/RAM:/";
const char *const delim = " \t\r\n";

static void bcbus_check_load_put_tty(int iface, char **line)
{
	char *s;
	int rc;
	size_t len, nput;

	struct _bc_bus_stats *bs = &bc_bus_stats[iface];

	if (strncmp(*line, "#exit", 5) == 0) {
		bcbus_stop_shell_bypass(bs->sh);
	} else if (strncmp(*line, "#debrick", 8) == 0) {
		bcbus_send_debrick(iface, bs->client_ba);
		LOG_INF("debrick sent");

	} else if (strncmp(*line, "#load", 5) == 0) {
		if (bs->fjob != NULL) {
			return;
		}
		strtok(*line, delim);
		s = strtok(NULL, delim);
		rc = new_bcbus_send_file_job(&bs->fjob, s, true);
		if (rc != 0) {
			LOG_ERR("Error new_bcbus_send_file_job:%d", rc);
		}
		/* TODO: load and send line by line *
		 * k_sem_reset(&bs->response_sem); *
		 * k_sem_give(&bs->response_sem); *
		 * if (k_sem_take(&bs->response_sem, K_MSEC(150)) != 0) { *
		 *	LOG_WRN("Wait for TTY timeout");
		 * } */

	} else {
		s = *line;
		len = strlen(s);
		nput = 0;
		while (1) {
			nput += bcbus_tty_client_put(iface, bs->client_ba, &s[nput], len - nput);
			if (len == nput) {
				break;
			}
			k_sleep(K_MSEC(10));
		}
	}
}

static void bcbus_bypass_cb(const struct shell *sh, uint8_t *keydata, size_t len)
{
	size_t bytes_written = 0;
	int n;
	int iface;
	char c;

	iface = bcbus_get_iface_by_shell(sh);
	struct _bc_bus_stats *bs = &bc_bus_stats[iface];

	for (n = 0; n < len; n++) {
		c = keydata[n];
		if (c == 127) {
			c = '\b';
		}
		if (c != '\n') {
			echo_char = c;
		}
	}

	if (iface >= 0) {
		if (keydata[0] == 0x03) {
			bcbus_stop_shell_bypass(sh);
			return;
		}

		for (n = 0; n < 100; ++n) {
			bytes_written += bcbus_linebuf(&bs->line_in, keydata, len);
			if (bs->line_in.line != NULL) {
				if (bs->fjob == NULL) {
					bcbus_check_load_put_tty(iface, &bs->line_in.line);
				}
				free(bs->line_in.line);
				bs->line_in.line = NULL;
			}
			if (bytes_written >= len) {
				break;
			}
			k_sleep(K_MSEC(1));
		}

		if (bytes_written != len) {
			PR_ERROR(sh, "BC-Bus no response ! - exit slaveconnect\n");
			bcbus_stop_shell_bypass(sh);
			return;
		}
	}
}

static int cmd_bcbus_connect(const struct shell *sh, size_t argc, char **argv)
{
	int client_iface;
	uint8_t client_ba;
	const char *client_name;
	struct _bc_bus_stats *bs;

	if (argc < 3) {
		return -EINVAL;
	}

	client_name = get_busname_idx(atoi(argv[1]));
	client_iface = bcbus_iface_get_by_name(client_name);
	client_ba = atoi(argv[2]);
	int res = bcbus_tty_client_new_connect(client_iface);
	bs = &bc_bus_stats[client_iface];
	bs->client_ba = client_ba;
	bs->sh = sh;

	PR_SHELL(sh, "--- Connect %s Adr%d ---\n", client_name, client_ba);
	shell_set_bypass(sh, bcbus_bypass_cb);

	return res;
}
char *tftp_server(void)
{
	int rc;
	struct in_addr tftp_ipv4;
	const struct device *eeprom = get_eeprom_device();
	static char buf[20];

	if (eeprom == NULL) {
		return NULL;
	}

	rc = eeprom_read(eeprom, EEPROM_TFTP_ADR, &tftp_ipv4, sizeof(tftp_ipv4));
	if (rc < 0) {
		printk("Error: Couldn't read eeprom: err: %d.\n", rc);
		return buf;
	}

	net_addr_ntop(AF_INET, &tftp_ipv4, buf, 20);

	return buf;
}

static int cmd_repo_ip(const struct shell *sh, size_t argc, char **argv)
{
	int rc = 0;
	struct in_addr tftp_ipv4;
	const struct device *eeprom = get_eeprom_device();
	char *sServer;

	sServer = tftp_server();

	if (argc < 2) {
		PR_SHELL(sh, "--- IPv4 Adr repo: %s ---\n", sServer);
	} else {
		rc = net_addr_pton(AF_INET, argv[1], &tftp_ipv4);
		if (rc != 0) {
			PR_SHELL(sh, "\n--- invalid IPv4 Adr: %s ---\n", argv[1]);
			return rc;
		}
		rc = eeprom_write(eeprom, EEPROM_TFTP_ADR, &tftp_ipv4, sizeof(tftp_ipv4));
		if (rc < 0) {
			printk("Error: Couldn't write eeprom: err: %d.\n", rc);
			return rc;
		}
		PR_SHELL(sh, "\n--- IPv4 Adr repo: %s ---\n", argv[1]);
	}

	return rc;
}

/* BUS BUS BUS */

static int start_script(const struct shell *sh, const char *path)
{
	struct fs_dirent dirent;
	struct fs_file_t file;
	int err, read, pos;
	char linebuf[80], c;

	err = fs_stat(path, &dirent);
	if (err < 0) {
		shell_error(sh, "Failed to obtain file %s (err: %d)", path, err);
		return -EIO;
	}

	if (dirent.type != FS_DIR_ENTRY_FILE) {
		shell_error(sh, "Not a file %s", path);
		return -EIO;
	}

	err = fs_open(&file, path, FS_O_READ);
	if (err < 0) {
		shell_error(sh, "Failed to open %s (%d)", path, err);
		return -EIO;
	}
	pos = 0;
	while (true) {
		read = fs_read(&file, &c, 1);
		if (read <= 0) {
			break;
		}
		if (c == '\r' || c == '\n') {
			linebuf[pos] = '\0';
			pos = 0;
			shell_execute_cmd(sh, linebuf);

		} else {
			linebuf[pos++] = c;
		}
	}

	if (read < 0) {
		shell_error(sh, "Failed to read from file %s (err: %zd)", path, read);
	}

	fs_close(&file);
}

int bcbus_mb_services_start(void);

static void start_bc(void)
{
	int n;
	char startstr[80];
	const struct shell *sh = shell_backend_uart_get_ptr();

	struct wdt_timeout_cfg wdt_config = {
		/* Reset SoC when watchdog timer expires. */
		.flags = WDT_FLAG_RESET_SOC,

		/* Expire watchdog after max window */
		.window.min = WDT_MIN_WINDOW,
		.window.max = WDT_MAX_WINDOW,
	};

	wdt_channel_id = wdt_install_timeout(wdt, &wdt_config);
	if (wdt_channel_id < 0) {
		LOG_ERR("Watchdog install error");
	}

	polling_init();

	for (n = 0; n < COUNT_BUS; n++) {
		snprintf(startstr, sizeof(startstr), "bcbus load %d", n);
		shell_execute_cmd(sh, startstr);
		snprintf(startstr, sizeof(startstr), "bcbus start %d", n);
		shell_execute_cmd(sh, startstr);
	}

	bcbus_mb_services_start();

	PR_SHELL(sh, "%s", startstr);

	return;

	start_script(NULL, "/lfs/etc/startup");
}

#ifdef CONFIG_APP_LITTLEFS_STORAGE_FLASH
static int littlefs_flash_erase(unsigned int id, bool format)
{
	const struct flash_area *pfa;
	int rc;

	rc = flash_area_open(id, &pfa);
	if (rc < 0) {
		LOG_ERR("FAIL: unable to find flash area %u: %d\n", id, rc);
		return rc;
	}

	LOG_PRINTK("Area %u at 0x%x on %s for %u bytes\n", id, (unsigned int)pfa->fa_off,
		   pfa->fa_dev->name, (unsigned int)pfa->fa_size);

	/* Optional wipe flash contents */
	if (format) {
		rc = flash_area_flatten(pfa, 0, pfa->fa_size);
		LOG_ERR("Erasing flash area ... %d", rc);
	}

	flash_area_close(pfa);
	return rc;
}

#define PARTITION_NODE DT_NODELABEL(lfs1)

FS_LITTLEFS_DECLARE_DEFAULT_CONFIG(storage);
static struct fs_mount_t lfs_storage_mnt = {
	.type = FS_LITTLEFS,
	.fs_data = &storage,
	.storage_dev = (void *)FIXED_PARTITION_ID(storage_partition),
	.mnt_point = "/lfs",
};

static FATFS fat_fs;

static struct fs_mount_t fatfs_ramdisk_mnt = {
	.type = FS_FATFS,
	.fs_data = &fat_fs,
	.storage_dev = (void *)FIXED_PARTITION_ID(rampart0),
	.mnt_point = "/RAM:",
};

struct fs_mount_t *spi_flash = &lfs_storage_mnt

	;

static int littlefs_mount(struct fs_mount_t *mp, bool format)
{
	int rc;

	rc = littlefs_flash_erase((uintptr_t)mp->storage_dev, format);
	if (rc < 0) {
		return rc;
	}

	/* Do not mount if auto-mount has been enabled */
#if !DT_NODE_EXISTS(PARTITION_NODE) ||                                                             \
	!(FSTAB_ENTRY_DT_MOUNT_FLAGS(PARTITION_NODE) & FS_MOUNT_FLAG_AUTOMOUNT)
	rc = fs_mount(mp);
	if (rc < 0) {
		LOG_PRINTK("FAIL: mount id %" PRIuPTR " at %s: %d\n", (uintptr_t)mp->storage_dev,
			   mp->mnt_point, rc);
		return rc;
	}
	LOG_PRINTK("%s mount: %d\n", mp->mnt_point, rc);
#else
	LOG_PRINTK("%s automounted\n", mp->mnt_point);
#endif

	return 0;
}
#endif /* CONFIG_APP_LITTLEFS_STORAGE_FLASH */

static int cmd_bcbus_factory(const struct shell *sh, size_t argc, char **argv)
{
	int rc;
	rc = fs_unmount(spi_flash);
	if (rc < 0) {
		LOG_PRINTK("FAIL: unmount id %" PRIuPTR " at %s: %d\n",
			   (uintptr_t)spi_flash->storage_dev, spi_flash->mnt_point, rc);
		return rc;
	}

	rc = littlefs_mount(spi_flash, true);
	if (rc < 0) {
		return rc;
	}

	return 0;
}

SHELL_CMD_ARG_REGISTER(mem, NULL, "Memory Status", cmd_memstat, 1, 0);
SHELL_CMD_ARG_REGISTER(forth, NULL, "Forth Switch", cmd_forth, 1, 0);
SHELL_CMD_ARG_REGISTER(wdog, NULL, "WDOG Reset auslösen", cmd_wdog_reset, 1, 0);
SHELL_CMD_ARG_REGISTER(pvis, NULL, "PVIS poll time", cmd_pvis_time, 2, 0);
SHELL_STATIC_SUBCMD_SET_CREATE(
	sub_bcbus, SHELL_CMD_ARG(start, NULL, "Start Polling [bus]", cmd_poll_start, 2, 0),
	SHELL_CMD_ARG(stop, NULL, "Stop Polling [bus]", cmd_poll_stop, 2, 0),
	SHELL_CMD_ARG(status, NULL, "Print Status Bus [bus]", cmd_bus_status, 2, 0),
	SHELL_CMD_ARG(snr, NULL, "Poll SNr [bus] [ba]", cmd_poll_snr, 3, 0),
	SHELL_CMD_ARG(connect, NULL, "Connect BCbus Device [bus] [ba]", cmd_bcbus_connect, 3, 0),
	SHELL_CMD_ARG(scan, NULL, "Scan BCbus [bus] [start] [end]", cmd_scan_bus, 4, 0),
	SHELL_CMD_ARG(list, NULL, "List BCbus [bus]", cmd_list_bus, 2, 0),
	SHELL_CMD_ARG(remove, NULL, "Remove item from BCbus [bus] [busadr]", cmd_delete_ba, 3, 0),
	SHELL_CMD_ARG(save, NULL, "Save BCbus configuration [bus]", cmd_config_save_bus, 2, 0),
	SHELL_CMD_ARG(load, NULL, "Load BCbus configuration [bus]", cmd_config_load_bus, 2, 0),
	SHELL_CMD_ARG(factory, NULL, "Erase configuration [id]", cmd_bcbus_factory, 2, 0),
	SHELL_CMD_ARG(repo, NULL, "TFTP Server forth [IPv4]", cmd_repo_ip, 1, 1),
	SHELL_SUBCMD_SET_END);
SHELL_CMD_REGISTER(bcbus, &sub_bcbus, "BC-Bus commands", NULL);

int main(void)
{
	LOG_INF("Starting mecrisp gateway");
	net_if_foreach(set_mac_addr, NULL);

	if (!pwm_is_ready_dt(&red_pwm_led) || !pwm_is_ready_dt(&green_pwm_led) ||
	    !pwm_is_ready_dt(&blue_pwm_led)) {
		printk("Error: one or more PWM devices not ready\n");
		return 0;
	}

	/* wait_for_network(); */

	LOG_INF("Run dhcpv4 client");

	net_mgmt_init_event_callback(&mgmt_cb, (net_mgmt_event_handler_t)net_cb_handler,
				     NET_EVENT_IPV4_ADDR_ADD);
	net_mgmt_add_event_callback(&mgmt_cb);

	net_dhcpv4_init_option_callback(&dhcp_cb, option_handler, DHCP_OPTION_NTP, ntp_server,
					sizeof(ntp_server));

	net_dhcpv4_add_option_callback(&dhcp_cb);

	net_if_foreach(start_dhcpv4_client, NULL);

	littlefs_mount(spi_flash, false);

	fs_mkfs(FS_FATFS, (uintptr_t)"RAM:", NULL, 0);
	fs_mount(&fatfs_ramdisk_mnt);

	if (init_bcbus_clients()) {
		LOG_ERR("BC-Bus initialization failed");
		return 0;
	}

	/* der HTTP Server besizt die PVIS Abbilder
	 *
	 */
	start_http_server();

	start_bc();

	return 0;
}
