#ifndef firmware__h
#define firmware__h
#include <stdio.h>

#include <zephyr/storage/flash_map.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_SIZE_FIRMWARE FIXED_PARTITION_SIZE(slot1_partition)

void firmware_update_start(void);
size_t write_firmware_blk(void *data, size_t slen);
void firmware_update_abort(void);
void firmware_update_confirm(void);

#ifdef __cplusplus
}
#endif

#endif
