

#ifdef CONFIG_ARCH_POSIX
#include <unistd.h>
#else
#include <zephyr/posix/unistd.h>
#endif

#include <fcntl.h>
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(tftpload_c, LOG_LEVEL_DBG);
#include <zephyr/fs/fs.h>
#include <zephyr/fs/littlefs.h>

#include <zephyr/net/net_if.h>
#include <zephyr/net/tftp.h>

#include <zephyr/sys/ring_buffer.h>
#include "tftpload.h"

struct _tftp_context {
	struct tftpc client;
	int fd;
};

static struct _tftp_context tftpo;

static void tftp_event_callback(const struct tftp_evt *evt)
{
	ssize_t written = 0;

	switch (evt->type) {
	case TFTP_EVT_DATA:
		do {
			written = write(tftpo.fd, evt->param.data.data_ptr,
					evt->param.data.len - written);
			if (written < 0) {
				LOG_ERR("Write Error: %d", written);
				return;
			}
		} while (written < evt->param.data.len);
		break;
	case TFTP_EVT_ERROR:
		LOG_ERR("Error code %d msg: %s", evt->param.error.code, evt->param.error.msg);
	default:
		break;
	}
}

static int mk_missing_dirs(const char *path, const char *base)
{
	char *str, *s;
	struct stat statBuf;
	const char ch = '/';
	char *arg = strdup(path);

	if (strchr(arg, ch) == NULL) {
		LOG_ERR("not a path: %s", arg);
		free(arg);
		return -1;
	}
	for (s = arg + strlen(arg); *s != ch; --s)
		;
	*s = '\0';

	s = arg;
	while ((str = strtok(s, "/")) != NULL) {
		if (str != s) {
			str[-1] = '/';
		}
		if (stat(arg, &statBuf) == -1) {
			if ((int)strlen(arg) - (int)strlen(base) > 1) {
				mkdir(arg, 0);
			}
		} else {
			if (!S_ISDIR(statBuf.st_mode)) {
				LOG_ERR("couldn't create directory %s", arg);
				free(arg);
				return -1;
			}
		}
		s = NULL;
	}
	free(arg);
	return 0;
}

int tftpload_fname(const char *hostname, const char *fname, const char *basepath)
{
	struct sockaddr remote_addr;
	struct addrinfo *res, hints = {0};
	struct stat filestat;

	int ret;
	size_t slen;

	/* Setup TFTP server address */
	hints.ai_socktype = SOCK_DGRAM;
	ret = getaddrinfo(hostname, CONFIG_TFTP_APP_PORT, &hints, &res);

	if (ret != 0) {
		LOG_ERR("Unable to resolve address");
		/* DNS error codes don't align with normal errors */
		return -ENOENT;
	}

	memcpy(&remote_addr, res->ai_addr, sizeof(remote_addr));
	freeaddrinfo(res);

	memcpy(&tftpo.client.server, &remote_addr, sizeof(tftpo.client.server));
	tftpo.client.callback = tftp_event_callback;

	slen = strlen(fname) + strlen(basepath) + 1;
	char *fullname = malloc(slen);
	if (fullname == NULL) {
		return -ENOMEM;
	}

	strcpy(fullname, basepath);
	strcat(fullname, fname);

	mk_missing_dirs(fullname, basepath);

	if (stat(fullname, &filestat) == 0) {
		unlink(fullname);
	}

	tftpo.fd = open(fullname, O_WRONLY | O_CREAT);
	free(fullname);

	ret = tftp_get(&tftpo.client, fname, "octet");

	close(tftpo.fd);

	if (ret < 0) {
		LOG_ERR("Error whlile getting file (%d)", ret);
		return ret;
	}

	return 0;
}
