
#include <zephyr/kernel.h>

#include <zephyr/bcbus/bcbus.h>
#include <zephyr/bcbus/bcbus_unit.h>
#include "bcbus_util.h"

#define LINE1_XADR 0x0100
#define LINE2_XADR 0x0120

/* size of stack area used by each thread */
#define STACKSIZE 1024

/* scheduling priority used by each thread */
#define PRIORITY 13

struct _prv_remotectrl {
	struct remotectrl *rp;
	int akt_ba;
	int akt_iface;
};

struct k_sem update_sem;
struct remotectrl *rp_modul = NULL;

static int get_xdata_via_bus(struct _prv_remotectrl *rp, uint16_t offset)
{
	struct bcbus_tele bustele = {
		.txbuf = {0, 4, SEND_XDATA, offset & 0xff, offset >> 8, 8},
	};
	int rc = -1, m;

	char *p;

	for (m = 0; m < 3; ++m) {
		rc = bcbus_do_client_handshake(rp->akt_iface, rp->akt_ba, &bustele);
		if (rc == 0) {
			break;
		}
	}
	if (rc < 0) {
		strcpy(rp->rp->line1, "...!bus time out!...");
		strcpy(rp->rp->line2, "...!bus time out!...");
		return rc;
	}

	if (offset >= LINE2_XADR) {
		p = rp->rp->line2 + (offset - LINE2_XADR);
	} else {
		p = rp->rp->line1 + (offset - LINE1_XADR);
	}
	for (int i = 0; i < bustele.rxbuf[1]; ++i) {
		*p = bustele.rxbuf[i + 2];
		p++;
	}
	return 0;
}

static int put_scode_via_bus(struct _prv_remotectrl *rp)
{
	struct bcbus_tele bustele = {
		.txbuf =
			{
				0,
				2,
				SET_KEY,
				rp->rp->scancode,
			},
	};
	return bcbus_do_client_handshake(rp->akt_iface, rp->akt_ba, &bustele);
}

int set_bcbus_remote_ba(struct remotectrl *rp, uint8_t ba)
{
	int iface;

	iface = bcbus_find_ba_iface(ba);
	rp->iface = iface;
	rp->ba = ba;

	return iface;
}

int bcbus_remote_fernbedienung(struct remotectrl *remote)
{
	if (remote->ba != 0 && remote->iface >= 0) {
		rp_modul = remote;
		k_sem_give(&update_sem);

	} else {
		rp_modul = NULL;
	}
	return 0;
}

void do_fernbedienung(void *dummy1, void *dummy2, void *dummy3)
{
	ARG_UNUSED(dummy1);
	ARG_UNUSED(dummy2);
	ARG_UNUSED(dummy3);

	struct _prv_remotectrl remote;

	int rc;

	k_sem_init(&update_sem, 0, 2);

	while (1) {
		if (rp_modul != NULL && rp_modul->ba != 0) {
			remote.rp = rp_modul;
			remote.akt_ba = rp_modul->ba;
			remote.akt_iface = rp_modul->iface;
		} else {
			remote.akt_ba = 0;
		}
		if (remote.akt_ba == remote.rp->ba && remote.akt_iface >= 0 && remote.akt_ba != 0) {
			rc = get_xdata_via_bus(&remote, LINE1_XADR + 0);
			rc = get_xdata_via_bus(&remote, LINE1_XADR + 8);
			rc = get_xdata_via_bus(&remote, LINE1_XADR + 16);
			rc = get_xdata_via_bus(&remote, LINE2_XADR + 0);
			rc = get_xdata_via_bus(&remote, LINE2_XADR + 8);
			rc = get_xdata_via_bus(&remote, LINE2_XADR + 16);
			if (remote.rp->scancode != 0) {
				rc = put_scode_via_bus(&remote);
				remote.rp->scancode = 0;
			}
		}
		k_sem_take(&update_sem, K_FOREVER);
	}
}

K_THREAD_DEFINE(thread_fernb, STACKSIZE, do_fernbedienung, NULL, NULL, NULL, PRIORITY, 0, 10);
