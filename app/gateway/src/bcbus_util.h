#ifndef bcbus_util__h
#define bcbus_util__h

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <zephyr/bcbus/bcbus_unit.h>

struct remotectrl {
	uint8_t ba;
	char line1[25];
	char line2[25];
	uint8_t scancode;
	int idle_count;
	int iface;
};

int set_bcbus_remote_ba(struct remotectrl *rp, uint8_t ba);
int bcbus_remote_fernbedienung(struct remotectrl *rp);

#ifdef __cplusplus
}
#endif

#endif
