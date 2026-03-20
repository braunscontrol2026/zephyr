/*
 *
 */

#include "psram.h"
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(bcmodbus_c, CONFIG_MODBUS_LOG_LEVEL);

#include <zephyr/modbus/modbus.h>
#include <zephyr/kernel.h>
#include <zephyr/net/socket.h>
#include <zephyr/bcbus/bcbus_unit.h>
#include <sys/select.h>
#include "polling.h"

/* size of stack area used by each thread */
#define STACKSIZE 2048

/* scheduling priority used by each thread */
#define PRIORITY 14

#define MODBUS_TCP_PORT 502

#define MB_REGS_PER_UNIT 32

#define MB_TCP_MAX_CONNECTIONS 32

struct _image_prv {
	int server_iface;
	struct modbus_adu tmp_adu;
	bool running;
};

struct _tcp_connection {
	int fd;
	struct k_timer time_out;
};

K_SEM_DEFINE(received, 0, 1);

static struct _image_prv _lm;

static int server_raw_cb(const int iface, const struct modbus_adu *adu, void *user_data)
{
	LOG_DBG("Server raw callback from interface %d", iface);

	_lm.tmp_adu.trans_id = adu->trans_id;
	_lm.tmp_adu.proto_id = adu->proto_id;
	_lm.tmp_adu.length = adu->length;
	_lm.tmp_adu.unit_id = adu->unit_id;
	_lm.tmp_adu.fc = adu->fc;
	memcpy(_lm.tmp_adu.data, adu->data, MIN(adu->length, CONFIG_MODBUS_BUFFER_SIZE));

	LOG_HEXDUMP_DBG(_lm.tmp_adu.data, _lm.tmp_adu.length, "resp");
	k_sem_give(&received);

	return 0;
}

#define MB_INVALID_UNIT -1
#define MB_CACHE_TO_MS  5

struct mb_cache {
	int act_unit;
	uint8_t buf[BCBUS_MAPPING];
};
static struct mb_cache _mb_cache_prv = {.act_unit = -1};

K_TIMER_DEFINE(mb_cache_timer, NULL, NULL);

static uint8_t *set_mb_cache_unit(int unit)
{

	uint8_t *rc = NULL;
	uint8_t *dp;

	if (unit != MB_INVALID_UNIT) {
		if (unit != _mb_cache_prv.act_unit || k_timer_status_get(&mb_cache_timer) > 0) {
			dp = (uint8_t *)PSRAM_POLL_DATA2_ADR((uint8_t)unit);
			psmemcpy(&_mb_cache_prv.buf, dp, BCBUS_MAPPING);
		}
		rc = _mb_cache_prv.buf;
		k_timer_user_data_set(&mb_cache_timer, &_mb_cache_prv);
		k_timer_start(&mb_cache_timer, K_MSEC(MB_CACHE_TO_MS), K_NO_WAIT);
	}

	_mb_cache_prv.act_unit = unit;
	return rc;
}

static int input_reg_rd(uint16_t addr, uint16_t *reg)
{
	uint8_t mb_off;

	if (addr >= 256 * MB_REGS_PER_UNIT) {
		return -ENOTSUP;
	}

	mb_off = 2 * (addr % MB_REGS_PER_UNIT);
	*reg = sys_get_le16(set_mb_cache_unit(addr / MB_REGS_PER_UNIT) + mb_off);
	LOG_DBG("Input register read, addr %u", addr);
	return 0;
}

static int holding_reg_rd(uint16_t addr, uint16_t *reg)
{
	uint8_t mb_off;

	if (addr >= 256 * MB_REGS_PER_UNIT) {
		return -ENOTSUP;
	}

	mb_off = 2 * (addr % MB_REGS_PER_UNIT);
	*reg = sys_get_le16(set_mb_cache_unit(addr / MB_REGS_PER_UNIT) + mb_off);
	LOG_DBG("Holding register read, addr %u", addr);
	return 0;
}

static int holding_reg_wr(uint16_t addr, uint16_t reg)
{
	uint8_t mb_off;
	uint8_t *dp;
	uint8_t buf[2];

	if (addr >= 256 * MB_REGS_PER_UNIT) {
		return -ENOTSUP;
	}

	mb_off = 2 * (addr % MB_REGS_PER_UNIT);

	dp = (uint8_t *)PSRAM_POLL_DATA2_ADR(addr / MB_REGS_PER_UNIT) + mb_off;
	sys_put_le16(reg, buf);
	psmemcpy(dp, &buf, sizeof(buf));
	/* invalidate cache */
	set_mb_cache_unit(MB_INVALID_UNIT);
	LOG_DBG("Holding register write, addr %u", addr);

	return 0;
}

static struct modbus_user_callbacks mbs_cbs = {
	.holding_reg_rd = holding_reg_rd,
	.holding_reg_wr = holding_reg_wr,
	.input_reg_rd = input_reg_rd,
};

const static struct modbus_iface_param server_param = {
	.mode = MODBUS_MODE_RAW,
	.server =
		{
			.user_cb = &mbs_cbs,
			.unit_id = 1,
		},

	.rawcb.raw_tx_cb = server_raw_cb,
	.rawcb.user_data = NULL,
};

static int init_modbus_server(void)
{
	char iface_name[] = "RAW_0";
	int err;

	_lm.server_iface = modbus_iface_get_by_name(iface_name);

	if (_lm.server_iface < 0) {
		LOG_ERR("Failed to get iface index for %s", iface_name);
		return -ENODEV;
	}

	err = modbus_init_server(_lm.server_iface, server_param);

	if (err < 0) {
		return err;
	}

	return 0;
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
	modbus_raw_get_header(&_lm.tmp_adu, header);
	data_len = _lm.tmp_adu.length;

	rc = recv(client, _lm.tmp_adu.data, data_len, MSG_WAITALL);
	if (rc <= 0) {
		return rc == 0 ? -ENOTCONN : -errno;
	}

	LOG_HEXDUMP_DBG(_lm.tmp_adu.data, _lm.tmp_adu.length, "d:>");
	if (modbus_raw_submit_rx(_lm.server_iface, &_lm.tmp_adu)) {
		LOG_ERR("Failed to submit raw ADU");
		return -EIO;
	}

	if (k_sem_take(&received, K_MSEC(1000)) != 0) {
		LOG_ERR("MODBUS RAW wait time expired");
		modbus_raw_set_server_failure(&_lm.tmp_adu);
	}

	return modbus_tcp_reply(client, &_lm.tmp_adu);
}

#define MB_TCP_TIMEOUT_MS 5000

void thread_modbus_tcp_entry(void *dummy1, void *dummy2, void *dummy3)
{
	ARG_UNUSED(dummy1);
	ARG_UNUSED(dummy2);
	ARG_UNUSED(dummy3);
	int serv;
	struct sockaddr_in bind_addr;
	static int counter, i, sock_max, ready, max = -1;
	static struct _tcp_connection client_sock[MB_TCP_MAX_CONNECTIONS];
	static fd_set gesamt_sock, lese_sock;

	while (!_lm.running) {
		k_sleep(K_MSEC(1000));
	}

	sock_max = serv = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

	if (serv < 0) {
		LOG_ERR("error: socket: %d", errno);
		return;
	}

	bind_addr.sin_family = AF_INET;
	bind_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	bind_addr.sin_port = htons(MODBUS_TCP_PORT);

	if (bind(serv, (struct sockaddr *)&bind_addr, sizeof(bind_addr)) < 0) {
		LOG_ERR("error: bind: %d", errno);
		return;
	}

	if (listen(serv, 5) < 0) {
		LOG_ERR("error: listen: %d", errno);
		return;
	}

	LOG_INF("Started MODBUS TCP server on port %d", MODBUS_TCP_PORT);

	for (i = 0; i < MB_TCP_MAX_CONNECTIONS; i++) {
		client_sock[i].fd = -1;
		k_timer_init(&client_sock[i].time_out, NULL, NULL);
	}

	FD_ZERO(&gesamt_sock);
	FD_SET(serv, &gesamt_sock);

	while (1) {
		lese_sock = gesamt_sock;

		struct sockaddr_in client_addr;
		socklen_t client_addr_len = sizeof(client_addr);
		char addr_str[INET_ADDRSTRLEN];
		int client, sock3;
		int rc;

		ready = select(sock_max + 1, &lese_sock, NULL, NULL, NULL);

		if (FD_ISSET(serv, &lese_sock)) {
			client = accept(serv, (struct sockaddr *)&client_addr, &client_addr_len);

			if (client < 0) {
				LOG_ERR("error: accept: %d", errno);
				continue;
			}

			inet_ntop(client_addr.sin_family, &client_addr.sin_addr, addr_str,
				  sizeof(addr_str));
			LOG_DBG("Connection #%d from %s", counter++, addr_str);
			for (i = 0; i < FD_SETSIZE; i++) {
				if (client_sock[i].fd < 0) {
					client_sock[i].fd = client;
					k_timer_start(&client_sock[i].time_out,
						      K_MSEC(MB_TCP_TIMEOUT_MS), K_FOREVER);
					break;
				}
			}
			if (i == FD_SETSIZE) {
				LOG_ERR("To much Connections #%d from %s", counter, addr_str);
			} else {
				FD_SET(client, &gesamt_sock);
				if (client > sock_max) {
					sock_max = client;
				}
				if (i > max) {
					max = i;
				}
			}
			if (--ready <= 0) {
				continue;
			}
		} // if(FD_ISSET ...

		/* Ab hier werden alle Verbindungen von Clients auf
		 * die Ankunft von neuen Daten überprüft */
		for (i = 0; i <= max; i++) {
			if ((sock3 = client_sock[i].fd) < 0) {
				continue;
			}
			rc = 0;
			/* (Socket-)Deskriptor gesetzt ... */
			if (FD_ISSET(sock3, &lese_sock)) {
				/* ... dann die Daten lesen */
				rc = modbus_tcp_connection(sock3);
				/* Wenn quit erhalten wurde ... */
				if (rc == 0) {
					k_timer_start(&client_sock[i].time_out,
						      K_MSEC(MB_TCP_TIMEOUT_MS), K_FOREVER);
				}
				--ready;
			}
			if (rc != 0 || k_timer_status_get(&client_sock[i].time_out) > 0) {
				/* ... hat sich der Client beendet oder timeout */
				// Socket schließen
				close(sock3);
				// aus Menge löschen
				FD_CLR(sock3, &gesamt_sock);
				client_sock[i].fd = -1; // auf -1 setzen
				k_timer_stop(&client_sock[i].time_out);
				LOG_DBG("Connection from %s closed, errno %d", addr_str, rc);
			}
		}
	}
}

K_THREAD_DEFINE(thread_modbus, STACKSIZE, thread_modbus_tcp_entry, NULL, NULL, NULL, PRIORITY, 0,
		15);

int bcbus_mb_services_start(void)
{
	if (init_modbus_server()) {
		LOG_ERR("Modbus TCP server initialization failed");
		return 0;
	}

	_lm.running = true;

	return 0;
}
