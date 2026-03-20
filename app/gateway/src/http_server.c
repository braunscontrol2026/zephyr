#include "zephyr/net/http/method.h"
#include <stdlib.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(http_server_c, LOG_LEVEL_DBG);

#include <zephyr/kernel.h>
#include <zephyr/net/tls_credentials.h>
#include <zephyr/net/http/server.h>
#include <zephyr/net/http/service.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/net/socket.h>
#include <zephyr/device.h>
#include <zephyr/sys/util.h>
#include <zephyr/sys/reboot.h>
#include "http_server.h"
#include "polling.h"
#include "cJSON.h"
#include "urldecode.h"
#include "bcbus_util.h"
#include "firmware.h"

static uint8_t bview_bcr_html[] = {
#include "bview_bcr.html.gz.inc"
};

static uint8_t favicon_ico_gz[] = {
#include "favicon.ico.gz.inc"
};

static uint8_t fernb_bcr_html[] = {
#include "fernb_bcr.html.gz.inc"
};

static uint8_t fw_update_html[] = {
#include "fw_update.html.gz.inc"
};


struct http_pvis_ctx {
	int wdog_timer;
	struct k_timer wd_timer;
};

/* public port */
static struct http_pvis_ctx http_loc_ctx = {
	.wdog_timer = 30
};

struct http_resource_detail_static favicon_ico_gz_resource_detail = {
	.common =
		{
			.type = HTTP_RESOURCE_TYPE_STATIC,
			.bitmask_of_supported_http_methods = BIT(HTTP_GET),
			.content_encoding = "gzip",
		},
	.static_data = favicon_ico_gz,
	.static_data_len = sizeof(favicon_ico_gz),
};

static void parse_led_post(uint8_t *buf, size_t len)
{
}

static int led_handler(struct http_client_ctx *client, enum http_data_status status,
		       const struct http_request_ctx *request_ctx,
		       struct http_response_ctx *response_ctx, void *user_data)
{
	static uint8_t post_payload_buf[32];
	static size_t cursor;

	LOG_DBG("LED handler status %d, size %zu", status, request_ctx->data_len);

	if (status == HTTP_SERVER_DATA_ABORTED) {
		cursor = 0;
		return 0;
	}

	if (request_ctx->data_len + cursor > sizeof(post_payload_buf)) {
		cursor = 0;
		return -ENOMEM;
	}

	/* Copy payload to our buffer. Note that even for a small payload, it may arrive split into
	 * chunks (e.g. if the header size was such that the whole HTTP request exceeds the size of
	 * the client buffer).
	 */
	memcpy(post_payload_buf + cursor, request_ctx->data, request_ctx->data_len);
	cursor += request_ctx->data_len;

	if (status == HTTP_SERVER_DATA_FINAL) {
		parse_led_post(post_payload_buf, cursor);
		cursor = 0;
	}

	return 0;
}

static struct http_resource_detail_dynamic led_resource_detail = {
	.common =
		{
			.type = HTTP_RESOURCE_TYPE_DYNAMIC,
			.bitmask_of_supported_http_methods = BIT(HTTP_POST),
		},
	.cb = led_handler,
	.user_data = NULL,
};

static int uptime_handler(struct http_client_ctx *client, enum http_data_status status,
			  const struct http_request_ctx *request_ctx,
			  struct http_response_ctx *response_ctx, void *user_data)
{
	int ret;
	static uint8_t uptime_buf[sizeof(STRINGIFY(INT64_MAX))];

	LOG_DBG("Uptime handler status %d", status);

	/* A payload is not expected with the GET request. Ignore any data and wait until
	 * final callback before sending response
	 */
	if (status == HTTP_SERVER_DATA_FINAL) {
		ret = snprintf(uptime_buf, sizeof(uptime_buf), "%" PRId64, k_uptime_get());
		if (ret < 0) {
			LOG_ERR("Failed to snprintf uptime, err %d", ret);
			return ret;
		}

		response_ctx->body = uptime_buf;
		response_ctx->body_len = ret;
		response_ctx->final_chunk = true;
	}

	return 0;
}

static int upl_firmware_handler(struct http_client_ctx *client, enum http_data_status status,
				const struct http_request_ctx *request_ctx,
				struct http_response_ctx *response_ctx, void *user_data)
{
	static size_t cursor;

	if (status == HTTP_SERVER_DATA_ABORTED) {
		LOG_DBG("Upload Firmware Abort %d, size %zu", status, request_ctx->data_len);

		firmware_update_abort();
		cursor = 0;
		return 0;
	}

	if (cursor == 0) {
		LOG_DBG("Upload Firmware Start %d, size %zu", status, request_ctx->data_len);
		firmware_update_start();
	}

	if (cursor + request_ctx->data_len <= MAX_SIZE_FIRMWARE) {
		cursor += write_firmware_blk(request_ctx->data, request_ctx->data_len);
	} else {
		LOG_DBG("Upload Firmware Size %d, size %zu", status,
			request_ctx->data_len + cursor);
		firmware_update_abort();
		cursor = 0;
		return -ENOMEM;
	}

	if (status == HTTP_SERVER_DATA_FINAL) {
		LOG_DBG("Upload Firmware Success %d, size %zu", status, cursor);
		firmware_update_confirm();
		cursor = 0;
	}

	return 0;
}

static struct http_resource_detail_dynamic fw_updload_resource_detail = {
	.common =
		{
			.type = HTTP_RESOURCE_TYPE_DYNAMIC,
			.bitmask_of_supported_http_methods = BIT(HTTP_POST),
			.content_type = "application/octet-stream",
		},
	.cb = upl_firmware_handler,
	.user_data = NULL,
};

static struct http_resource_detail_dynamic uptime_resource_detail = {
	.common =
		{
			.type = HTTP_RESOURCE_TYPE_DYNAMIC,
			.bitmask_of_supported_http_methods = BIT(HTTP_GET),
		},
	.cb = uptime_handler,
	.user_data = NULL,
};

#define UP_BUF_SIZE 1280
static int upstream_get_handler(struct http_client_ctx *client, enum http_data_status status,
				const struct http_request_ctx *request_ctx,
				struct http_response_ctx *response_ctx, void *user_data)
{
	size_t len;
	static size_t cursor = 0;
	static uint8_t buf[UP_BUF_SIZE];

	/* A payload is not expected with the GET request. Ignore any data and wait until
	 * final callback before sending response
	 */
	if (status == HTTP_SERVER_DATA_FINAL) {
		k_timer_start(&http_loc_ctx.wd_timer, K_SECONDS(http_loc_ctx.wdog_timer), K_FOREVER);
		len = read_upstream_chunk(cursor, buf, UP_BUF_SIZE);
		cursor += len;
		response_ctx->body = buf;
		response_ctx->body_len = len;
		response_ctx->final_chunk = cursor >= PVIS_SIZE;
		if (response_ctx->final_chunk) {
			cursor = 0;
		}
	}

	return 0;
}

static struct http_resource_detail_dynamic upstream_resource_detail = {
	.common =
		{
			.type = HTTP_RESOURCE_TYPE_DYNAMIC,
			.bitmask_of_supported_http_methods = BIT(HTTP_GET),
			.content_type = "application/octet-stream",
		},
	.cb = upstream_get_handler,
	.user_data = NULL,
};

static int downstream_post_handler(struct http_client_ctx *client, enum http_data_status status,
				   const struct http_request_ctx *request_ctx,
				   struct http_response_ctx *response_ctx, void *user_data)
{
	static size_t cursor;
	static bool valid;

	if (status == HTTP_SERVER_DATA_ABORTED) {
		cursor = 0;
		return 0;
	}

	if (cursor == 0) {
		valid = request_ctx->data[0] == '*';
	}

	if (cursor + request_ctx->data_len > PVIS_SIZE) {
		LOG_ERR("File too large : %d bytes", cursor + request_ctx->data_len);
		return -ENOMEM;
	}

	/* Copy payload to our buffer. Note that even for a small payload, it may arrive split into
	 * chunks (e.g. if the header size was such that the whole HTTP request exceeds the size of
	 * the client buffer).
	 */
	if (valid) {
		cursor += write_setpoint_chunk(cursor, request_ctx->data, request_ctx->data_len);
	} else {
		cursor += request_ctx->data_len;
	}

	if (status == HTTP_SERVER_DATA_FINAL) {
		cursor = 0;
	}
	return 0;
}

static struct http_resource_detail_dynamic downstream_resource_detail = {
	.common =
		{
			.type = HTTP_RESOURCE_TYPE_DYNAMIC,
			.bitmask_of_supported_http_methods = BIT(HTTP_POST),
			.content_type = "application/octet-stream",
		},
	.cb = downstream_post_handler,
	.user_data = NULL,
};

static int send_json_response(struct http_response_ctx *response_ctx, cJSON *jdata,
			      char **static_str)
{
	int result = 0;
	/* Send JSON file to complete it */
	if (*static_str != NULL) {
		free(*static_str);
	}
	*static_str = cJSON_Print(jdata); /* allocates memory for result */
	if (static_str) {
		response_ctx->body = *static_str;
		response_ctx->body_len = strlen(*static_str);
		response_ctx->final_chunk = true;
	} else {
		result = -1;
	}

	return result;
}

struct static_bview {
	uint8_t ba;
	int idle_count;
};

static struct static_bview stat_bview_data;
static struct remotectrl fernb_remote = {0};

static void proc_JSON_bview_data(struct static_bview *rp, cJSON *obj)
{
	const cJSON *client = NULL;
	const cJSON *scode = NULL;
	client = cJSON_GetObjectItemCaseSensitive(obj, "clientData");
	if (client == NULL) {
		const char *error_ptr = cJSON_GetErrorPtr();
		if (error_ptr != NULL) {
			LOG_ERR("Error before: %s\n", error_ptr);
		}
		goto end;
	}
	scode = cJSON_GetObjectItemCaseSensitive(client, "ba");
	if (cJSON_IsNumber(scode)) {
		fernb_remote.ba = scode->valueint;
	}

end:;
}

static int bview_json_handler(struct http_client_ctx *client, enum http_data_status status,
			      const struct http_request_ctx *request_ctx,
			      struct http_response_ctx *response_ctx, void *user_data)
{
	struct static_bview *bview_data = (struct static_bview *)user_data;

	cJSON *jdata, *bview, *json_in, *aitem;
	struct busunit *punit;
	struct poll_data *pitem;
	char str[40];
	char *pos;
	static char *tstr = NULL;
	int rc;

	jdata = cJSON_CreateObject();
	if (jdata == 0) {
		return -1;
	}
	/* next Block */
	bview_data->ba = (bview_data->ba + 16) & 0xF0;

	cJSON_AddItemToObject(jdata, "module", cJSON_CreateString("busoverview"));
	cJSON_AddNumberToObject(jdata, "startba", bview_data->ba);
	cJSON_AddItemToObject(jdata, "segment", bview = cJSON_CreateArray());
	for (int ba = 0; ba < 16; ba++) {
		punit = get_polled_busunit(bview_data->ba + ba);
		pitem = (punit == NULL ? NULL : claim_poll_data_buf(punit->data));
		aitem = cJSON_CreateObject();
		snprintf(str, sizeof(str), "%02x %02x %02x %02x %02x %02x",
			 pitem == NULL ? 0 : pitem->data[2][2],
			 pitem == NULL ? 0 : pitem->data[2][3],
			 pitem == NULL ? 0 : pitem->data[2][4],
			 pitem == NULL ? 0 : pitem->data[2][5],
			 pitem == NULL ? 0 : pitem->data[2][6],
			 pitem == NULL ? 0 : pitem->data[2][7]);
		cJSON_AddStringToObject(aitem, "snr", str);
		cJSON_AddNumberToObject(aitem, "errcount", pitem == NULL ? 0 : pitem->data[2][1]);
		cJSON_AddItemToArray(bview, aitem);
		if (pitem != NULL) {
			release_poll_data_buf(punit->data, pitem);
		}
	}

	pos = strstr(request_ctx->data, "?data=");
	if (pos) {
		pos += sizeof("?data=") - 1;
		if (tstr != NULL) {
			free(tstr);
		}
		tstr = urlDecode(pos); /* allocates memory for tstr ! */
		if (tstr) {
			json_in = cJSON_Parse(tstr);
			proc_JSON_bview_data(bview_data, json_in);
			cJSON_Delete(json_in);
			free(tstr);
			tstr = NULL;
		}
	}

	rc = send_json_response(response_ctx, jdata, &tstr);

	cJSON_Delete(jdata);

	return rc;
}

static struct http_resource_detail_static fw_update_resource_detail = {
	.common =
		{
			.type = HTTP_RESOURCE_TYPE_STATIC,
			.bitmask_of_supported_http_methods = BIT(HTTP_GET),
			.content_encoding = "gzip",
		},
	.static_data = fw_update_html,
	.static_data_len = sizeof(fw_update_html),
};

static struct http_resource_detail_static bview_resource_detail = {
	.common =
		{
			.type = HTTP_RESOURCE_TYPE_STATIC,
			.bitmask_of_supported_http_methods = BIT(HTTP_GET),
			.content_encoding = "gzip",
		},
	.static_data = bview_bcr_html,
	.static_data_len = sizeof(bview_bcr_html),
};

static struct http_resource_detail_dynamic bview_json_resource_detail = {
	.common =
		{
			.type = HTTP_RESOURCE_TYPE_DYNAMIC,
			.bitmask_of_supported_http_methods = BIT(HTTP_GET),
			.content_type = "application/json",
		},
	.cb = bview_json_handler,
	.user_data = &stat_bview_data,
};

static void proc_JSON_fernb_data(struct remotectrl *rp, cJSON *obj)
{
	const cJSON *client = NULL;
	const cJSON *scode = NULL;
	client = cJSON_GetObjectItemCaseSensitive(obj, "clientData");
	if (client == NULL) {
		const char *error_ptr = cJSON_GetErrorPtr();
		if (error_ptr != NULL) {
			LOG_ERR("Error before: %s\n", error_ptr);
		}
		goto end;
	}
	scode = cJSON_GetObjectItemCaseSensitive(client, "scancode");
	if (!cJSON_IsNumber(scode)) {
		goto end;
	}
	if (scode->valueint != 0) {
		rp->scancode = scode->valueint;
	}
	scode = cJSON_GetObjectItemCaseSensitive(client, "ba");
	if (!cJSON_IsNumber(scode)) {
		goto end;
	}

	set_bcbus_remote_ba(rp, scode->valueint);
end:;
}

static int fernb_json_handler(struct http_client_ctx *client, enum http_data_status status,
			      const struct http_request_ctx *request_ctx,
			      struct http_response_ctx *response_ctx, void *user_data)
{
	struct remotectrl *remote = (struct remotectrl *)user_data;
	char *pos;
	static char *tstr = NULL;
	int rc;

	cJSON *jdata, *fernb, *json_in;
	jdata = cJSON_CreateObject();
	cJSON_AddItemToObject(jdata, "module", cJSON_CreateString("remotecontrol"));
	cJSON_AddNumberToObject(jdata, "free_heap", (double)0);
	cJSON_AddItemToObject(jdata, "client", fernb = cJSON_CreateObject());
	cJSON_AddStringToObject(fernb, "line1", remote->line1);
	cJSON_AddStringToObject(fernb, "line2", remote->line2);
	cJSON_AddNumberToObject(fernb, "idle_count", remote->idle_count);
	cJSON_AddNumberToObject(fernb, "busaddress", remote->ba);

	pos = strstr(request_ctx->data, "?data=");
	if (pos) {
		pos += sizeof("?data=") - 1;
		if (tstr != NULL) {
			free(tstr);
		}
		tstr = urlDecode(pos); /* allocates memory for tstr ! */
		if (tstr) {
			//			json_in = cJSON_Parse(trim_inner_string(tstr));
			json_in = cJSON_Parse(tstr);
			proc_JSON_fernb_data(remote, json_in);
			cJSON_Delete(json_in);
			free(tstr);
			tstr = NULL;
		}
	}

	bcbus_remote_fernbedienung(remote);

	rc = send_json_response(response_ctx, jdata, &tstr);

	cJSON_Delete(jdata);

	return rc;
}

static struct http_resource_detail_static fernb_resource_detail = {
	.common =
		{
			.type = HTTP_RESOURCE_TYPE_STATIC,
			.bitmask_of_supported_http_methods = BIT(HTTP_GET),
			.content_encoding = "gzip",
		},
	.static_data = fernb_bcr_html,
	.static_data_len = sizeof(fernb_bcr_html),
};

static struct http_resource_detail_dynamic fernb_json_resource_detail = {
	.common =
		{
			.type = HTTP_RESOURCE_TYPE_DYNAMIC,
			.bitmask_of_supported_http_methods = BIT(HTTP_GET),
			.content_type = "application/json",
		},
	.cb = fernb_json_handler,
	.user_data = &fernb_remote,
};

static uint16_t bcbus_http_service_port = CONFIG_APP_HTTP_SERVER_PORT;
HTTP_SERVER_CONTENT_TYPE(ico, "image/x-icon")

HTTP_RESOURCE_DEFINE(favicon_resource, bcbus_http_service, "/favicon.ico",
		     &favicon_ico_gz_resource_detail);

HTTP_SERVICE_DEFINE(bcbus_http_service, NULL, &bcbus_http_service_port,
		    CONFIG_HTTP_SERVER_MAX_CLIENTS, 10, NULL, NULL, NULL);

HTTP_RESOURCE_DEFINE(bview_json_resource, bcbus_http_service, "/bview.json",
		     &bview_json_resource_detail);
HTTP_RESOURCE_DEFINE(fernb_json_resource, bcbus_http_service, "/fernb.json",
		     &fernb_json_resource_detail);
HTTP_RESOURCE_DEFINE(bview_resource, bcbus_http_service, "/bview", &bview_resource_detail);
HTTP_RESOURCE_DEFINE(fernb_resource, bcbus_http_service, "/fernb", &fernb_resource_detail);
HTTP_RESOURCE_DEFINE(uptime_resource, bcbus_http_service, "/uptime", &uptime_resource_detail);
HTTP_RESOURCE_DEFINE(update_resource, bcbus_http_service, "/update", &fw_update_resource_detail);

HTTP_RESOURCE_DEFINE(uplfw_resource, bcbus_http_service, "/uploadfw", &fw_updload_resource_detail);
HTTP_RESOURCE_DEFINE(led_resource, bcbus_http_service, "/led", &led_resource_detail);

HTTP_RESOURCE_DEFINE(ubstream_resource, bcbus_http_service, "/api/v1/bc-2000/upstream.bin",
		     &upstream_resource_detail);

HTTP_RESOURCE_DEFINE(downstream_resource, bcbus_http_service, "/api/v1/bc-2000/downstream.bin",
		     &downstream_resource_detail);

void set_pvis_time(int seconds)
{
	k_timer_stop(&http_loc_ctx.wd_timer);
	http_loc_ctx.wdog_timer = seconds;
};

static void wdog_expiry(struct k_timer *timer_id)
{
	sys_reboot(SYS_REBOOT_WARM);
}

int start_http_server(void)
{
	http_server_start();
	k_timer_init(&http_loc_ctx.wd_timer, wdog_expiry, NULL);
	return 0;
}
