
#include "zephyr/toolchain.h"
#include <stdbool.h>
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(sendfile_c, LOG_LEVEL_DBG);

#include <zephyr/kernel.h>
#include <zephyr/version.h>
#include <zephyr/linker/sections.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#ifdef CONFIG_ARCH_POSIX
#include <unistd.h>
#else
#include <zephyr/posix/unistd.h>
#endif
#include <fcntl.h>
#include <zephyr/fs/fs.h>
#include <zephyr/fs/littlefs.h>

#include <zephyr/sys/ring_buffer.h>

#include <zephyr/bcbus/bcbus.h>
#include <zephyr/bcbus/bcbus_unit.h>
#include "sendfile.h"
#include "tftpload.h"

/* size of stack area used by each thread */
#define STACKSIZE 2048

/* scheduling priority used by each thread */
#define PRIORITY 14

K_THREAD_STACK_DEFINE(sendfile_thread_stack0, STACKSIZE);
K_THREAD_STACK_DEFINE(sendfile_thread_stack1, STACKSIZE);
K_THREAD_STACK_DEFINE(sendfile_thread_stack2, STACKSIZE);
K_THREAD_STACK_DEFINE(sendfile_thread_stack3, STACKSIZE);

#define KSTACKSIZE K_THREAD_STACK_SIZEOF(sendfile_thread_stack0)

static k_tid_t th_tids[4] = {0};

struct _send_file_prv {
	struct sendfile_job pub;
	k_tid_t htid;
	int tid_idx;
	struct k_thread hthread_data;
	struct k_sem response_sem;
	struct sendfile_job **job;
};

static int next_free_tid(void)
{
	int n;

	for (n = 0; n < 4; n++) {
		if (th_tids[n] == NULL) {
			return n;
		}
	}
	return -1;
}

static void *get_stack(int n)
{
	switch (n) {
	case 0:
		return sendfile_thread_stack0;
	case 1:
		return sendfile_thread_stack1;
	case 2:
		return sendfile_thread_stack2;
	case 3:
		return sendfile_thread_stack3;
	}
	return NULL;
}

char *trim_leading_ctrl(char *str)
{
	char *start = str;

	while (iscntrl((unsigned char)*start) && (*start != '\0')) {
		start++;
	}
	memmove(str, start, strlen(start) + 1);

	return str;
}

char *trim_ctrl(char *str)
{
	char *end;
	char *start = str;

	while (iscntrl((unsigned char)*start) && (*start != '\0')) {
		start++;
	}
	memmove(str, start, strlen(start) + 1);

	end = str + strlen(str);
	while ((end != str) && iscntrl(*(end - 1))) {
		--end;
	}
	*end = '\0';

	return str;
}

char *trim_spaces(char *str)
{
	char *end;
	char *start = str;

	while (isspace((unsigned char)*start)) {
		start++;
	}
	memmove(str, start, strlen(start) + 1);

	end = str + strlen(str);
	while ((end != str) && isspace(*(end - 1))) {
		--end;
	}
	*end = '\0';

	return str;
}

char *trim_paran(char *str)
{
	char *open;
	char *close;

	open = strstr(str, " ( ");
	if (open != NULL) {
		close = strchr(open, ')');
		if (close != NULL) {
			memmove(open, close + 1, strlen(close));
		}
	}
	return str;
}

char *trim_comment(char *str)
{
	char *pos = str;

	while (*pos != '\0' && *pos != '\\') {
		++pos;
	}
	*pos = '\0';
	return str;
}

char *trim_forthline(char *str)
{
	char *p;

	trim_comment(str);
	trim_paran(str);
	trim_spaces(str);
	p = str + strlen(str);
	*p++ = '\r';
	*p = '\0';
	return str;
}

void bcbus_sendfile_check_response(struct sendfile_job *job)
{
	struct _send_file_prv *prv = (struct _send_file_prv *)job;
	int n;
	size_t len;

	len = strlen(job->send_line) - 1;

	n = strncmp(job->send_line, job->response_line, len);
	if (n == 0) {
		job->str_result = &job->response_line[len + 1];
	} else {
		job->str_result = NULL;
	}
	k_sem_give(&prv->response_sem);
}

extern const char *const bpath;
extern const char *const delim;
char *tftp_server(void);

static int process_file(struct _send_file_prv *prv, const char *fname)
{
	int fd;
	char linebuf[CONFIG_BCBUS_LINE_SIZE];
	int pos, rc;
	char c;
	const char *cr = "\r";
	int retry_count;

	rc = tftpload_fname(tftp_server(), fname, bpath);
	if (rc != 0) {
		LOG_ERR("Error TFTP load:%d", rc);
		return -1;
	}

	strcpy(linebuf, bpath);
	strcat(linebuf, fname);

	fd = open(linebuf, O_RDONLY);

	if (fd < 0) {
		LOG_ERR("open failed: %s", fname);
		return -1;
	}

	pos = 0;
	while ((rc = read(fd, &c, 1)) > 0) {

		if (c != '\r') {
			if (c == '\n') {
				c = '\r';
			}
			linebuf[pos++] = c;

			if ((c == '\r') || (pos > CONFIG_BCBUS_LINE_SIZE - 2)) {
				linebuf[pos] = '\0';
				pos = 0;
				if (strncmp(linebuf, "#load", 5) == 0) {
					strtok(linebuf, delim);
					/* rekursiver Abstieg ... */
					if (process_file(prv, strtok(NULL, delim)) < 0) {
						return -1;
					}
				} else {
					/* TODO: hier Wiederholungen im Fehlerfall*/
					if (prv->pub.str_result) {
						trim_forthline(linebuf);
					}
					prv->pub.line_to_send = linebuf;
					for (retry_count = 0; retry_count < 15; retry_count++) {
						k_sem_reset(&prv->response_sem);
						if (k_sem_take(&prv->response_sem, K_MSEC(500)) !=
						    0) {
							prv->pub.line_to_send = (char *)cr;
						}
						if (prv->pub.str_result != NULL) {
							trim_spaces(prv->pub.str_result);
							if (strchr(prv->pub.str_result, '?') ==
							    NULL) {
								break;
							} else {
								return -1;
							}
						}
					}
					if (prv->pub.str_result == NULL) {
						LOG_WRN("response timeout");
						return -1;
					}
				}
			}
		}
	}

	close(fd);
	return 0;
}

static void send_file_handler(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	struct _send_file_prv *prv = (struct _send_file_prv *)p1;

	k_sem_init(&prv->response_sem, 0, 1);
	process_file(prv, prv->pub.fname);
	*prv->job = NULL;
	th_tids[prv->tid_idx] = NULL;
	free(prv);
}

int new_bcbus_send_file_job(struct sendfile_job **job, const char *fname, bool trim)
{
	struct _send_file_prv *prv;
	int n;

	prv = calloc(1, sizeof(struct _send_file_prv));

	if (prv != NULL) {
		prv->job = job;
		*prv->job = (struct sendfile_job *)prv;
		strncpy(prv->pub.fname, fname, PATH_LEN_SENDFILE - 1);
		n = next_free_tid();
		if (n < 0) {
			free(prv);
			LOG_ERR("Too many threads");
			return -1;
		}
		prv->pub.trim_send_line = trim;
		prv->tid_idx = n;
		th_tids[n] =
			k_thread_create(&prv->hthread_data, get_stack(n), KSTACKSIZE,
					send_file_handler, prv, NULL, NULL, PRIORITY, 0, K_NO_WAIT);
	} else {
		LOG_ERR("sendfile - no memory");
		return -ENOMEM;
	}
	return 0;
}
