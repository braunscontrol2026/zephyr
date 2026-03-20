#ifndef polling__h
#define polling__h

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <stddef.h>
#include <zephyr/bcbus/bcbus_unit.h>

enum poll_state {
	WAIT_START = 0,
	START_POLL,
	RUNNING,
	STOP_POLL,
	NOT_VALID
};

#define PSRAM_START_ADR         (uint8_t *)0x04000000
#define PSRAM_POLL_DATA_ADR(n)  (PSRAM_START_ADR + (n) * sizeof(struct poll_data))
#define PSRAM_POLL_DATA2_ADR(n) (PSRAM_POLL_DATA_ADR(n) + 2 * BCBUS_MAPPING)

void start_polling(int iface);
void stop_polling(int iface);
enum poll_state status_polling(int iface);
const char *poll_state_txt(enum poll_state state);
size_t write_setpoint_chunk(uint16_t offset, uint8_t *data, size_t len);
size_t read_upstream_chunk(uint16_t offset, uint8_t *buf, size_t bufsize);

struct busunit *get_polled_busunit(uint8_t ba);
struct poll_data *claim_poll_data_buf(const struct poll_data *src);
void release_poll_data_buf(struct poll_data *dest, const struct poll_data *src);

char *get_poll_stats(char *buf, size_t buf_size, int iface, uint8_t ba);

void polling_init(void);

#ifdef __cplusplus
}
#endif

#endif
