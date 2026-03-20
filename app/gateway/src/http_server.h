#ifndef http_server__h
#define http_server__h

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#define PVIS_SIZE (256 * 64)

void set_pvis_time(int seconds);

int start_http_server(void);

#ifdef __cplusplus
}
#endif

#endif
