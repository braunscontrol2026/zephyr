#include <stdint.h>
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(nd6520_prot_c, LOG_LEVEL_DBG);

#include <zephyr/kernel.h>
#include <zephyr/version.h>
#include <string.h>
#include <limits.h>

#ifdef CONFIG_ARCH_POSIX
#include <unistd.h>
#else
#include <zephyr/posix/unistd.h>
#endif

#include <zephyr/bcbus/bcbus.h>
#include <zephyr/bcbus/bcbus_unit.h>

struct hanshake_ctx {
	uint8_t busadr;
	int iface;
	struct poll_data *dp;
	struct busunit *unit;
};

static bool doHandshake(struct hanshake_ctx *n, unsigned char *SendTele, unsigned char *RecTele)
{
	int res, len;
	struct bcbus_tele tele;

	memcpy(&tele.txbuf[1], SendTele, sizeof(tele.rxbuf) - 3);
	res = bcbus_do_client_handshake(n->iface, n->busadr, &tele);
	if (res == 0) {
		len = tele.rxbuf[1];
		if (len > BUS_MAX_DATA_V2) {
			len = BUS_MAX_DATA_V2;
		}
		memcpy(RecTele, &tele.rxbuf[1], len + 1);
	}
	return res == 0;
}
//---------------------------------------------------------------------------

uint8_t const SendTeleRG[][BUS_MAX_DATA] = {
	{8, SEND_RTEMP_S, 0, SEND_RTEMP_S, 1, SEND_RLTEMP_S, 0, SEND_RLTEMP_S, 1},
	{7, SEND_RTEMP, 0, SEND_RLTEMP, 0, SEND_VENTIL, 0, SEND_STATUS, 0},
	{6, SEND_RTEMP, 1, SEND_RLTEMP, 1, SEND_VENTIL, 1, 0, 0},
	{4, SET_RTEMP_S, 0, 0, 0, 0, 0, 0, 0},
	{4, SET_RTEMP_S, 1, 0, 0, 0, 0, 0, 0},
	{4, SET_STATBITS, 0, RES_STATBITS, 0, 0, 0, 0, 0},
	{4, SET_RLTEMP_S, 0, 0, 0, 0, 0, 0, 0},
	{4, SET_RLTEMP_S, 1, 0, 0, 0, 0, 0, 0}};

unsigned char const AnswerTeleRG[][BUS_MAX_DATA] = {
	{8, 24, 25, 34, 35, 32, 33, 36, 37}, {8, 8, 9, 10, 11, 12, 13, 63, 61},
	{6, 16, 17, 18, 19, 20, 21, 0, 0},   {0, 0, 0, 0, 0, 0, 0, 0, 0},
	{0, 0, 0, 0, 0, 0, 0, 0, 0},         {0, 0, 0, 0, 0, 0, 0, 0, 0},
};

unsigned char const SendTeleRG3[][BUS_MAX_DATA] = {
	{4, SEND_SHIFT_S, 0, SEND_SHIFT_S, 1},
	{11, SET_RTEMP_SC, 0, 0, 0, SET_RTEMP_SC, 1, 0, 0, SET_COMFORT_S, 0, 0}};

unsigned char const AnswerTeleRG3[][BUS_MAX_DATA] = {
	{4, 14, 15, 22, 23, 0, 0, 0, 0},
	{0, 0, 0, 0, 0, 0, 0, 0, 0},
};

unsigned char const SendTeleRGLI[][BUS_MAX_DATA] = {{11, SEND_RTEMP_S, 0, SEND_RLTEMP_S, 0,
						     SEND_RTEMP, 0, SEND_RLTEMP, 0, SEND_VENTIL, 0,
						     SEND_STATUS},
						    {4, SEND_SHIFT_S, 0, SEND_LSTAT, 1},
						    {7, SET_RTEMP_SC, 0, 0, 0, SET_COMFORT_S, 0, 0},
						    {4, SET_LTAST, 1, 0, 0}};

unsigned char const AnswerTeleRGLI[][BUS_MAX_DATA] = {
	{12, 24, 25, 32, 33, 8, 9, 10, 11, 12, 13, 63, 61},
	{4, 14, 15, 16, 17},
	{0, 0, 0, 0, 0, 0, 0, 0, 0}};

static void check_setpoint_change(struct poll_data *p, uint8_t offset)
{
	uint8_t *idata1 = &p->data[0][offset];
	uint8_t *idata2 = &p->data[1][offset];
	uint8_t *odata = &p->data[2][offset];

	if (*idata1 != *idata2) {
		*odata = *idata2 = *idata1;
	}
}

static void check_setpoint_change_16b(struct poll_data *p, uint8_t offset)
{
	uint16_t *idata1 = (uint16_t *)&p->data[0][offset];
	uint16_t *idata2 = (uint16_t *)&p->data[1][offset];
	uint16_t *odata = (uint16_t *)&p->data[2][offset];

	if (*idata1 != *idata2) {
		*odata = *idata2 = *idata1;
	}
}

static int poll_RGLI(struct hanshake_ctx *n)
{
	unsigned char RecTele[BUS_MAX_DATA];
	unsigned char SendTele[BUS_MAX_DATA];

	uint8_t *idata1 = n->dp->data[0];
	uint8_t *idata2 = n->dp->data[1];
	uint8_t *odata = n->dp->data[2];

	int err_count = 0;
	int m, j;

	for (j = 0; j < 2; j++) {
		memcpy(SendTele, SendTeleRGLI[j], sizeof(SendTele));
		for (m = 3; !doHandshake(n, SendTele, RecTele) && --m != 0;)
			;
		if (m == 0) {
			err_count++;
		} else if (RecTele[0] == AnswerTeleRGLI[j][0]) {
			for (int i = 1; i < AnswerTeleRGLI[j][0] + 1; i++) {
				odata[AnswerTeleRGLI[j][i]] = RecTele[i];
			}
		}
	}
	// Sollwert Kanal 1 - SET_RTEMP_S K1
	check_setpoint_change_16b(n->dp, 48);
	if ((odata[48] | odata[49]) != 0) {
		memcpy(SendTele, SendTeleRG[3], sizeof(SendTele));
		SendTele[3] = odata[48];
		SendTele[4] = odata[49];
		for (m = 3; !doHandshake(n, SendTele, RecTele) && --m != 0;)
			;
		if (m == 0) {
			err_count++;
		}
	}
	// Statusbits
	memcpy(SendTele, SendTeleRG[5], sizeof(SendTele));
	check_setpoint_change(n->dp, 62);
	SendTele[2] = odata[62] & 0x22;
	SendTele[4] = ~odata[62] & 0x22;

	for (m = 3; !doHandshake(n, SendTele, RecTele) && --m != 0;)
		;
	if (m == 0) {
		err_count++;
	}

	check_setpoint_change_16b(n->dp, 38); // SET_RTEMP_SC K1
	check_setpoint_change_16b(n->dp, 26); // SET_COMFORT_S

	if ((odata[38] | odata[39]) != 0) {
		memcpy(SendTele, SendTeleRGLI[2], sizeof(SendTele));
		SendTele[3] = odata[38]; // SET_RTEMP_SC K1
		SendTele[4] = odata[39];
		SendTele[6] = odata[26]; // SET_COMFORT_S
		SendTele[7] = odata[27];
		for (m = 3; !doHandshake(n, SendTele, RecTele) && --m != 0;)
			;
		if (m == 0) {
			err_count++;
		}
	}

	check_setpoint_change_16b(n->dp, 52);
	if ((odata[52] | odata[53]) != 0) {
		memcpy(SendTele, SendTeleRG[6], sizeof(SendTele));
		SendTele[3] = odata[52]; // SET_RLTEMP_S K1
		SendTele[4] = odata[53];
		for (m = 3; !doHandshake(n, SendTele, RecTele) && --m != 0;)
			;
		if (m == 0) {
			err_count++;
		}
	}

	if (idata1[50] != odata[50] || idata1[51] != odata[51]) {
		memcpy(SendTele, SendTeleRGLI[3], sizeof(SendTele));
		SendTele[3] = idata1[50]; // SET_LTAST K2
		SendTele[4] = idata1[51];
		for (m = 3; !doHandshake(n, SendTele, RecTele) && --m != 0;)
			;
		if (m == 0) {
			err_count++;
		} else {
			odata[50] = idata2[50] = idata1[50]; // SET_LTAST K2
			odata[51] = idata2[51] = idata1[51];
		}
	}
	odata[1] = MIN(UCHAR_MAX, err_count + odata[1]);
	return err_count;
}

static int poll_RG(struct hanshake_ctx *n)
{
	unsigned char RecTele[BUS_MAX_DATA];
	unsigned char SendTele[BUS_MAX_DATA];
	uint8_t *odata = n->dp->data[2];

	int err_count = 0;
	int m, j;

	for (j = 0; j < 3; j++) {
		memcpy(SendTele, SendTeleRG[j], sizeof(SendTele));
		for (m = 3; !doHandshake(n, SendTele, RecTele) && --m != 0;)
			;
		if (m == 0) {
			err_count++;
		} else if (RecTele[0] == AnswerTeleRG[j][0]) {
			for (int i = 1; i < AnswerTeleRG[j][0] + 1; i++) {
				odata[AnswerTeleRG[j][i]] = RecTele[i];
			}
		} else {
			// Anpassung für ältere Raumregler, die den Status noch nicht senden
			// SEND_STATUS 0x73 noch unbekannt
			if (j == 1 && RecTele[0] == 6) {
				for (int i = 1; i < RecTele[0] + 1; i++) {
					odata[AnswerTeleRG[j][i]] = RecTele[i];
				}
			}
		}
	}
	check_setpoint_change_16b(n->dp, 48);
	if ((odata[48] | odata[49]) != 0) {
		memcpy(SendTele, SendTeleRG[3], sizeof(SendTele));
		SendTele[3] = odata[48];
		SendTele[4] = odata[49];
		for (m = 3; !doHandshake(n, SendTele, RecTele) && --m != 0;)
			;
		if (m == 0) {
			err_count++;
		}
	}
	check_setpoint_change_16b(n->dp, 50);
	if ((odata[50] | odata[51]) != 0) {
		memcpy(SendTele, SendTeleRG[4], sizeof(SendTele));
		SendTele[3] = odata[50];
		SendTele[4] = odata[51];
		for (m = 3; !doHandshake(n, SendTele, RecTele) && --m != 0;)
			;
		if (m == 0) {
			err_count++;
		}
	}
	memcpy(SendTele, SendTeleRG[5], sizeof(SendTele));
	check_setpoint_change(n->dp, 62);
	SendTele[2] = odata[62] & 0x22;
	SendTele[4] = ~odata[62] & 0x22;
	for (m = 3; !doHandshake(n, SendTele, RecTele) && --m != 0;)
		;
	if (m == 0) {
		err_count++;
	}

	if (odata[3] >= 0x01) {
		// Neuer Reglertyp ab Dezember 2003 (SPOC)
		memcpy(SendTele, SendTeleRG3[0], sizeof(SendTele));
		for (m = 3; !doHandshake(n, SendTele, RecTele) && --m != 0;)
			;
		if (m == 0) {
			err_count++;
		} else if (RecTele[0] == AnswerTeleRG3[0][0]) {
			for (int i = 1; i < AnswerTeleRG3[0][0] + 1; i++) {
				odata[AnswerTeleRG3[0][i]] = RecTele[i];
			}
		}
		memcpy(SendTele, SendTeleRG3[1], sizeof(SendTele));
		check_setpoint_change_16b(n->dp, 0x26);
		SendTele[3] = odata[0x26];
		SendTele[4] = odata[0x27];
		check_setpoint_change_16b(n->dp, 0x28);
		SendTele[7] = odata[0x28];
		SendTele[8] = odata[0x29];
		check_setpoint_change_16b(n->dp, 0x1A);
		SendTele[10] = odata[0x1A];
		SendTele[11] = odata[0x1B];
		for (m = 3; !doHandshake(n, SendTele, RecTele) && --m != 0;)
			;
		if (m == 0) {
			err_count++;
		}
	}

	odata[1] = MIN(UCHAR_MAX, err_count + odata[1]);
	return err_count;
}

unsigned char const SendTeleCR[][BUS_MAX_DATA] = {
	{8, OUTVARS, 0, OUTVARS, 1, OUTVARS, 2, OUTVARS, 3},
	{8, OUTVARS, 4, OUTVARS, 5, OUTVARS, 6, OUTVARS, 7},
	{8, OUTVARS, 8, OUTVARS, 9, OUTVARS, 10, OUTVARS, 11},
	{8, OUTVARS, 12, OUTVARS, 13, OUTVARS, 14, OUTVARS, 15},
	{8, INVARS, 0, 0, 0, INVARS, 1, 0, 0},
	{8, INVARS, 2, 0, 0, INVARS, 3, 0, 0},
	{8, INVARS, 4, 0, 0, INVARS, 5, 0, 0},
	{8, INVARS, 6, 0, 0, INVARS, 7, 0, 0},
	{4, SET_CRTAST, 0, 0}};

unsigned char const AnswerTeleCR[][BUS_MAX_DATA] = {
	{8, 8, 9, 10, 11, 12, 13, 14, 15},   {8, 16, 17, 18, 19, 20, 21, 22, 23},
	{8, 24, 25, 26, 27, 28, 29, 30, 31}, {8, 32, 33, 34, 35, 36, 37, 38, 39},
	{0, 0, 0, 0, 0, 0, 0, 0, 0},         {0, 0, 0, 0, 0, 0, 0, 0, 0},
	{0, 0, 0, 0, 0, 0, 0, 0, 0},         {0, 0, 0, 0, 0, 0, 0, 0, 0},
	{0, 0, 0, 0, 0, 0, 0, 0, 0}};

unsigned char const SendTeleCR3[BUS_MAX_DATA_V2] = {58, BLK_XCHG, 1};

static int poll_CR3(struct hanshake_ctx *n)
{
	unsigned char RecTele[BUS_MAX_DATA_V2];
	unsigned char SendTele[BUS_MAX_DATA_V2];
	uint8_t *odata = n->dp->data[2];

	int err_count = 0;
	int m;

	memcpy(SendTele, SendTeleCR3, sizeof(SendTele));
	for (int i = 0; i < 56; i++) {
		if (i % 2 == 0 && i > 47 - 8) {
			check_setpoint_change_16b(n->dp, i + 8);
		}
		SendTele[i + 3] = odata[i + 8];
	}
	for (m = 3; !doHandshake(n, SendTele, RecTele) && --m != 0;)
		;
	if (m == 0) {
		err_count++;
	} else if (RecTele[0] == 56) {
		for (int i = 0; i < 36; i++) {
			odata[i + 8] = RecTele[i + 1];
		}
	}

	odata[1] = MIN(UCHAR_MAX, err_count + odata[1]);
	return err_count;
}

static int poll_CR2(struct hanshake_ctx *n)
{
	unsigned char RecTele[BUS_MAX_DATA];
	unsigned char SendTele[BUS_MAX_DATA];
	uint8_t *odata = n->dp->data[2];

	int err_count = 0;
	int m, j;

	for (j = 2; j < 4; j++) {
		memcpy(SendTele, SendTeleCR[j], sizeof(SendTele));
		for (m = 3; !doHandshake(n, SendTele, RecTele) && --m != 0;)
			;
		if (m == 0) {
			err_count++;
		} else if (RecTele[0] == AnswerTeleCR[j][0]) {
			for (int i = 1; i < AnswerTeleCR[j][0] + 1; i++) {
				odata[AnswerTeleCR[j][i]] = RecTele[i];
			}
		}
	}

	odata[1] = MIN(UCHAR_MAX, err_count + odata[1]);
	return err_count;
}

static int poll_CR1(struct hanshake_ctx *n)
{
	unsigned char RecTele[BUS_MAX_DATA];
	unsigned char SendTele[BUS_MAX_DATA];
	uint8_t *odata = n->dp->data[2];

	int err_count = 0;
	int m, j;

	for (j = 0; j < 2; j++) {
		memcpy(SendTele, SendTeleCR[j], sizeof(SendTele));
		for (m = 3; !doHandshake(n, SendTele, RecTele) && --m != 0;)
			;
		if (m == 0) {
			err_count++;
		} else if (RecTele[0] == AnswerTeleCR[j][0]) {
			for (int i = 1; i < AnswerTeleCR[j][0] + 1; i++) {
				odata[AnswerTeleCR[j][i]] = RecTele[i];
			}
		}
	}
	for (j = 0; j < 4; j++) {
		memcpy(SendTele, SendTeleCR[j + 4], sizeof(SendTele));
		check_setpoint_change_16b(n->dp, j * 4 + 48);
		SendTele[3] = odata[j * 4 + 48];
		SendTele[4] = odata[j * 4 + 49];
		check_setpoint_change_16b(n->dp, j * 4 + 50);
		SendTele[7] = odata[j * 4 + 50];
		SendTele[8] = odata[j * 4 + 51];
		for (m = 3; !doHandshake(n, SendTele, RecTele) && --m != 0;)
			;
		if (m == 0) {
			err_count++;
		}
	}

	odata[1] = MIN(UCHAR_MAX, err_count + odata[1]);
	return err_count;
}

static int poll_CR_SW(struct hanshake_ctx *n)
{
	unsigned char RecTele[BUS_MAX_DATA];
	unsigned char SendTele[BUS_MAX_DATA];
	uint8_t *idata1 = n->dp->data[0];
	uint8_t *idata2 = n->dp->data[1];
	uint8_t *odata = n->dp->data[2];

	int err_count = 0;
	int m;

	// Schaltbefehle setzen
	memcpy(SendTele, SendTeleCR[8], sizeof(SendTele));
	if (idata1[46] != odata[46] || idata1[47] != odata[47]) {
		SendTele[2] = idata1[46];
		SendTele[3] = idata1[47];
		for (m = 3; !doHandshake(n, SendTele, RecTele) && --m != 0;)
			;
		if (m == 0) {
			err_count++;
		} else {
			odata[46] = idata2[46] = idata1[46];
			odata[47] = idata2[47] = idata1[47];
		}
	}
	// Schaltbefehle Modbus setzen
	memcpy(SendTele, SendTeleCR[8], sizeof(SendTele));
	if (odata[44] != 0 || odata[45] != 0) {
		SendTele[2] = odata[44];
		SendTele[3] = odata[45];
		for (m = 3; !doHandshake(n, SendTele, RecTele) && --m != 0;)
			;
		if (m == 0) {
			err_count++;
		} else {
			odata[44] = 0;
			odata[45] = 0;
		}
	}

	odata[1] = MIN(UCHAR_MAX, err_count + odata[1]);
	return err_count;
}

unsigned char const SendTeleSR2[][BUS_MAX_DATA] = {
	{8, SEND_VTEMP, 0, SEND_RLTEMP, 0, SEND_VENTIL, 0, SEND_ATEMP, 0},
	{4, SEND_VTEMP_S, 0, SEND_PUMP, 0},
	{6, SET_BETR_STAT, 0, SET_VTEMP_S, 0, 0, 0},
	{8, SET_VERBUND, 0, 0, 0, 0, 0, 0, 0},
	{4, SEND_ANAIN, 2, SEND_ANAIN, 3}};

unsigned char const AnswerTeleSR2[][BUS_MAX_DATA] = {{8, 8, 9, 10, 11, 12, 13, 14, 15},
						     {4, 16, 17, 32, 33},
						     {0, 0},
						     {0, 0},
						     {4, 18, 19, 20, 21}};

static int poll_SR2(struct hanshake_ctx *n)
{
	unsigned char RecTele[BUS_MAX_DATA];
	unsigned char SendTele[BUS_MAX_DATA];
	uint8_t *odata = n->dp->data[2];

	int err_count = 0;
	int m, j;

	for (j = 0; j < 2; j++) {
		memcpy(SendTele, SendTeleSR2[j], sizeof(SendTele));
		for (m = 3; !doHandshake(n, SendTele, RecTele) && --m != 0;)
			;
		if (m == 0) {
			err_count++;
		} else if (RecTele[0] == AnswerTeleSR2[j][0]) {
			for (int i = 1; i < AnswerTeleSR2[j][0] + 1; i++) {
				odata[AnswerTeleSR2[j][i]] = RecTele[i];
			}
		}
	}
	// Sollwerte setzen
	memcpy(SendTele, SendTeleSR2[2], sizeof(SendTele));
	check_setpoint_change(n->dp, 28);
	SendTele[2] = odata[28];
	check_setpoint_change_16b(n->dp, 24);
	SendTele[5] = odata[24];
	SendTele[6] = odata[25];
	for (m = 3; !doHandshake(n, SendTele, RecTele) && --m != 0;)
		;
	if (m == 0) {
		err_count++;
	}
	// Verbundwerte setzen
	memcpy(SendTele, SendTeleSR2[3], sizeof(SendTele));
	for (j = 0; j < 6; j++) {
		check_setpoint_change(n->dp, 48 + j);
		SendTele[j + 3] = odata[48 + j];
	}
	for (m = 3; !doHandshake(n, SendTele, RecTele) && --m != 0; err_count++)
		;
	if (m == 0) {
		err_count++;
	}
	// Eingänge Ain2 und Ain3 holen
	j = 4;
	memcpy(SendTele, SendTeleSR2[j], sizeof(SendTele));
	for (m = 3; !doHandshake(n, SendTele, RecTele) && --m != 0;)
		;
	if (m == 0) {
		err_count++;
	} else if (RecTele[0] == AnswerTeleSR2[j][0]) {
		for (int i = 1; i < AnswerTeleSR2[j][0] + 1; i++) {
			odata[AnswerTeleSR2[j][i]] = RecTele[i];
		}
	}

	odata[1] = MIN(UCHAR_MAX, err_count + odata[1]);
	return err_count;
}

unsigned char const SendTeleUG[][BUS_MAX_DATA] = {
	{8, SEND_ANAIN, 0, SEND_ANAIN, 1, SEND_ANAIN, 2, SEND_ANAIN, 3},
	{8, SEND_ANAIN, 4, SEND_ANAIN, 5, SEND_ANAIN, 6, SEND_ANAIN, 7},
	{8, SEND_PWM, 0, SEND_PWM, 1, SEND_DO, 0, SEND_DO, 1},
	{9, SEND_ANAO, 0, SEND_ANAO, 1, SEND_3PO, 0, SEND_3PO, 1, SEND_STATUS},
	{8, SETZE_SOLL, 0, 0, 0, SETZE_SOLL, 1, 0, 0},
};
unsigned char const AnswerTeleUG[][BUS_MAX_DATA] = {{8, 8, 9, 10, 11, 12, 13, 14, 15},
						    {8, 16, 17, 18, 19, 20, 21, 22, 23},
						    {8, 32, 33, 34, 35, 36, 37, 38, 39},
						    {10, 24, 25, 26, 27, 28, 29, 30, 31, 62, 63},
						    {0}};

static int poll_UG(struct hanshake_ctx *n)
{
	unsigned char RecTele[BUS_MAX_DATA];
	unsigned char SendTele[BUS_MAX_DATA];
	uint8_t *odata = n->dp->data[2];

	int err_count = 0;
	int m, j;

	for (j = 0; j < 4; j++) {
		memcpy(SendTele, SendTeleUG[j], sizeof(SendTele));
		for (m = 3; !doHandshake(n, SendTele, RecTele) && --m != 0;)
			;
		if (m == 0) {
			err_count++;

		} else if (RecTele[0] == AnswerTeleUG[j][0]) {
			for (int i = 1; i < AnswerTeleUG[j][0] + 1; i++) {
				odata[AnswerTeleUG[j][i]] = RecTele[i];
			}
		}
	}
	memcpy(SendTele, SendTeleUG[4], sizeof(SendTele));
	SendTele[0] = 0;
	for (j = 0; j < 2; j++) {
		check_setpoint_change_16b(n->dp, 2 * j + 48);
		if ((odata[2 * j + 48] | odata[2 * j + 49]) != 0) {
			SendTele[0] += 4;
			SendTele[4 * j + 2] = j;
			SendTele[4 * j + 3] = odata[2 * j + 48];
			SendTele[4 * j + 4] = odata[2 * j + 49];
		}
	}
	if (SendTele[0] != 0) {
		for (m = 3; !doHandshake(n, SendTele, RecTele) && --m != 0;)
			;
		if (m == 0) {
			err_count++;
		}
	}
	memcpy(SendTele, SendTeleUG[4], sizeof(SendTele));
	SendTele[0] = 0;
	for (j = 0; j < 2; j++) {
		check_setpoint_change_16b(n->dp, 2 * j + 52);
		if ((odata[2 * j + 52] | odata[2 * j + 53]) != 0) {
			SendTele[0] += 4;
			SendTele[4 * j + 2] = j + 2;
			SendTele[4 * j + 3] = odata[2 * j + 52];
			SendTele[4 * j + 4] = odata[2 * j + 53];
		}
	}
	if (SendTeleUG[4][0] != 0) {
		for (m = 3; !doHandshake(n, SendTele, RecTele) && --m != 0;)
			;
		if (m == 0) {
			err_count++;
		}
	}

	odata[1] = MIN(UCHAR_MAX, err_count + odata[1]);
	return err_count;
}

unsigned char const SendTeleFG[][BUS_MAX_DATA] = {
	{8, SEND_ANAIN, 0, SEND_ANAIN, 1, SEND_ANAIN, 2, SEND_ANAIN, 3},
	{8, SEND_ANAIN, 4, SEND_ANAIN, 5, SEND_ANAIN, 6, SEND_ANAIN, 7},
	{8, SEND_DO, 0, SEND_DO, 1, SEND_DO, 2, SEND_DO, 3},
	{8, SEND_ANAO, 0, SEND_ANAO, 1, SEND_ANAO, 2, SEND_ANAO, 3},
	{8, SEND_DI, 0, SEND_DI, 1, SEND_DI, 2, SEND_DI, 3},
	{8, SETZE_SOLL, 0, 0, 0, SETZE_SOLL, 1, 0, 0},
};

unsigned char const AnswerTeleFG[][BUS_MAX_DATA] = {
	{8, 8, 9, 10, 11, 12, 13, 14, 15},   {8, 16, 17, 18, 19, 20, 21, 22, 23},
	{8, 40, 41, 42, 43, 44, 45, 46, 47}, {8, 24, 25, 26, 27, 28, 29, 30, 31},
	{8, 32, 33, 34, 35, 36, 37, 38, 39}, {0}};

static int poll_FG(struct hanshake_ctx *n)
{
	unsigned char RecTele[BUS_MAX_DATA];
	unsigned char SendTele[BUS_MAX_DATA];
	uint8_t *odata = n->dp->data[2];

	int err_count = 0;
	int m, j;

	for (j = 0; j < 5; j++) {
		memcpy(SendTele, SendTeleFG[j], sizeof(SendTele));
		for (m = 3; !doHandshake(n, SendTele, RecTele) && --m != 0;)
			;
		if (m == 0) {
			err_count++;
		} else if (RecTele[0] == AnswerTeleFG[j][0]) {
			for (int i = 1; i < AnswerTeleFG[j][0] + 1; i++) {
				odata[AnswerTeleFG[j][i]] = RecTele[i];
			}
		}
	}
	memcpy(SendTele, SendTeleFG[5], sizeof(SendTele));
	SendTele[0] = 0;
	for (j = 0; j < 2; j++) {
		check_setpoint_change_16b(n->dp, 2 * j + 48);
		if ((odata[2 * j + 48] | odata[2 * j + 49]) != 0) {
			SendTele[0] += 4;
			SendTele[4 * j + 2] = j;
			SendTele[4 * j + 3] = odata[2 * j + 48];
			SendTele[4 * j + 4] = odata[2 * j + 49];
		}
	}
	if (SendTele[0] != 0) {
		for (m = 3; !doHandshake(n, SendTele, RecTele) && --m != 0;)
			;
		if (m == 0) {
			err_count++;
		}
	}
	memcpy(SendTele, SendTeleFG[5], sizeof(SendTele));
	SendTele[0] = 0;
	for (j = 0; j < 2; j++) {
		check_setpoint_change_16b(n->dp, 52);
		if ((odata[2 * j + 52] | odata[2 * j + 53]) != 0) {
			SendTele[0] += 4;
			SendTele[4 * j + 2] = j + 2;
			SendTele[4 * j + 3] = odata[2 * j + 52];
			SendTele[4 * j + 4] = odata[2 * j + 53];
		}
	}
	if (SendTele[0] != 0) {
		for (m = 3; !doHandshake(n, SendTele, RecTele) && --m != 0;)
			;
		if (m == 0) {
			err_count++;
		}
	}

	odata[1] = MIN(UCHAR_MAX, err_count + odata[1]);
	return err_count;
}

unsigned char const SendTeleVS[][BUS_MAX_DATA] = {
	{8, SEND_VENT, 0, SEND_VENT, 1, SEND_BETRSTD, 0, SEND_BETRSTD, 1},
	{2, SET_BETR_STAT, 0},
};

unsigned char const AnswerTeleVS[][BUS_MAX_DATA] = {{8, 32, 33, 34, 35, 8, 9, 10, 11}, {1, 29}};

static int poll_VS(struct hanshake_ctx *n)
{
	unsigned char RecTele[BUS_MAX_DATA];
	unsigned char SendTele[BUS_MAX_DATA];
	uint8_t *odata = n->dp->data[2];

	int err_count = 0;
	int m;

	memcpy(SendTele, SendTeleVS[0], sizeof(SendTele));
	for (m = 3; !doHandshake(n, SendTele, RecTele) && --m != 0;)
		;
	if (m == 0) {
		err_count++;
	} else if (RecTele[0] == AnswerTeleVS[0][0]) {
		for (int i = 1; i < AnswerTeleVS[0][0] + 1; i++) {
			odata[AnswerTeleVS[0][i]] = RecTele[i];
		}
	}
	memcpy(SendTele, SendTeleVS[1], sizeof(SendTele));
	check_setpoint_change(n->dp, 28);
	SendTele[2] = odata[28];
	for (m = 3; !doHandshake(n, SendTele, RecTele) && --m != 0;)
		;
	if (m == 0) {
		err_count++;
	} else if (RecTele[0] == AnswerTeleVS[0][0]) {
		for (int i = 1; i < AnswerTeleVS[0][0] + 1; i++) {
			odata[AnswerTeleVS[0][i]] = RecTele[i];
		}
	}

	odata[1] = MIN(UCHAR_MAX, err_count + odata[1]);
	return err_count;
}

unsigned char const SendTeleLG[][BUS_MAX_DATA] = {
	{2, SEND_LSTAT, 0},
	{4, SET_LSOLL, 0, SET_LTAST, 0},
};

unsigned char const AnswerTeleLG[][BUS_MAX_DATA] = {{2, 8, 9}, {0}};

static int poll_LG(struct hanshake_ctx *n)
{
	unsigned char RecTele[BUS_MAX_DATA];
	unsigned char SendTele[BUS_MAX_DATA];
	uint8_t *idata1 = n->dp->data[0];
	uint8_t *idata2 = n->dp->data[1];
	uint8_t *odata = n->dp->data[2];

	int err_count = 0;
	int m;

	memcpy(SendTele, SendTeleLG[0], sizeof(SendTele));
	for (m = 3; !doHandshake(n, SendTele, RecTele) && --m != 0;)
		;
	if (m == 0) {
		err_count++;
	} else if (RecTele[0] == AnswerTeleLG[0][0]) {
		for (int i = 1; i < AnswerTeleLG[0][0] + 1; i++) {
			odata[AnswerTeleLG[0][i]] = RecTele[i];
		}
	}
	memcpy(SendTele, SendTeleLG[1], sizeof(SendTele));
	check_setpoint_change(n->dp, 28);
	SendTele[2] = odata[28];
	check_setpoint_change(n->dp, 48);
	if ((SendTele[4] = odata[48]) == 0) {
		SendTele[0] = 2;
	} else {
		SendTele[0] = 4;
		odata[48] = idata1[48] = idata2[48] = 0;
	}
	for (m = 3; !doHandshake(n, SendTele, RecTele) && --m != 0;)
		;
	if (m == 0) {
		err_count++;
	}

	odata[1] = MIN(UCHAR_MAX, err_count + odata[1]);
	return err_count;
}

unsigned char const SendTeleKS[][BUS_MAX_DATA] = {
	{8, SEND_VTEMP, 0, SEND_RLTEMP, 0, SEND_VENTIL, 0, SEND_ATEMP, 0},
	{4, SEND_RTEMP_S, 0, SEND_BRSTAT, 0},
	{6, SEND_COUNTER, 0, SEND_COUNTER, 1, SEND_COUNTER, 2},
	{10, SET_VERBUND, 0, 0, 0, 0, 0, 0, 0, SET_BETR_STAT, 0}};

unsigned char AnswerTeleKS[][BUS_MAX_DATA] = {
	{8, 8, 9, 10, 11, 12, 13, 14, 15}, {4, 16, 17, 32, 33}, {6, 18, 19, 20, 21, 22, 23}, {0}};

static int poll_KS(struct hanshake_ctx *n)
{
	unsigned char RecTele[BUS_MAX_DATA];
	unsigned char SendTele[BUS_MAX_DATA];
	uint8_t *odata = n->dp->data[2];

	int err_count = 0;
	int m, j;

	for (j = 0; j < 3; j++) {
		memcpy(SendTele, SendTeleKS[j], sizeof(SendTele));
		for (m = 3; !doHandshake(n, SendTele, RecTele) && --m != 0;)
			;
		if (m == 0) {
			err_count++;
		} else if (RecTele[0] == AnswerTeleKS[j][0]) {
			for (int i = 1; i < AnswerTeleKS[j][0] + 1; i++) {
				odata[AnswerTeleKS[j][i]] = RecTele[i];
			}
		}
	}
	// Betriebsstatus setzen
	memcpy(SendTele, SendTeleKS[3], sizeof(SendTele));
	check_setpoint_change(n->dp, 28);
	SendTele[10] = odata[28];
	// Verbundwerte setzen
	for (j = 0; j < 6; j++) {
		check_setpoint_change(n->dp, 48 + j);
		SendTele[j + 3] = odata[48 + j];
	}
	for (m = 3; !doHandshake(n, SendTele, RecTele) && --m != 0;)
		;
	if (m == 0) {
		err_count++;
	}

	odata[1] = MIN(UCHAR_MAX, err_count + odata[1]);
	return err_count;
}

static int handshake(struct hanshake_ctx *n)
{
	uint8_t *odata = n->dp->data[2];

	int err_count = 0;

	switch (odata[2]) {
	case RGLI_TYPE:
		err_count += poll_RGLI(n);
		break;

	case RG_TYPE:
		err_count += poll_RG(n);
		break;

	case SR_TYPE:
		/* MacheMeldung("Handshake SR_TYPE!"); */
		break;

	case CR3_TYPE:
		err_count += poll_CR3(n);
		err_count += poll_CR_SW(n);
		break;

	case CR2_TYPE:
		err_count += poll_CR2(n);
		/* NOBREAK */

	case CR1_TYPE:
		err_count += poll_CR1(n);
		err_count += poll_CR_SW(n);
		break;

	case SR2_TYPE:
		err_count += poll_SR2(n);
		break;

	case UG_TYPE:
		err_count += poll_UG(n);
		break;

	case FG_TYPE:
		err_count += poll_FG(n);
		break;

	case VS_TYPE:
		err_count += poll_VS(n);
		break;

	case LG_TYPE:
		err_count += poll_LG(n);
		break;

	case KS_TYPE:
		err_count += poll_KS(n);
		break;

	default:
		/* MacheMeldung("Unbekanntes Gerät!"); */
		err_count = 5;
		break;
	}
	if (err_count != 0) {
		n->unit->err_cnt++;
	}
	if (++n->unit->poll_cnt > UINT32_MAX / 2) {
		n->unit->poll_cnt /= 2;
		n->unit->err_cnt /= 2;
	}

	return err_count;
}
//---------------------------------------------------------------------------
int poll_busunit(int iface, struct busunit *unit)
{
	struct hanshake_ctx sr;
	sr.iface = iface;
	sr.busadr = unit->ba;
	sr.dp = unit->data;
	sr.unit = unit;
	return handshake(&sr);
}
