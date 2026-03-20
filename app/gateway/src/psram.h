#ifndef psram__h
#define psram__h

#ifdef __cplusplus
extern "C" {
#endif
#include <stdio.h>

void *psmemcpy(void *dest, const void *src, size_t size);
void *psmemset(void *s, int c, size_t size);
#ifdef __cplusplus
}
#endif

#endif
