#ifndef sendfile__h
#define sendfile__h

#include <stdbool.h>
#include <zephyr/kernel.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PATH_LEN_SENDFILE 80

struct sendfile_job {
	char fname[PATH_LEN_SENDFILE];
	char *send_line;
	char *line_to_send;
	char *response_line;
	char *str_result;
	struct k_sem response_sem;
	bool trim_send_line;
};

int new_bcbus_send_file_job(struct sendfile_job **job, const char *fname, bool trim);
void bcbus_sendfile_check_response(struct sendfile_job *job);

#ifdef __cplusplus
}
#endif

#endif
