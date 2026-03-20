#ifndef tftpload__h
#define tftpload__h

#ifdef __cplusplus
extern "C" {
#endif
#include <zephyr/net/tftp.h>

int tftpload_fname(const char *hostname, const char *fname, const char *basepath);

#ifdef __cplusplus
}
#endif

#endif
