
#define DT_DRV_COMPAT bc_sam0_qspiram

#define LOG_LEVEL CONFIG_SPI_LOG_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(sam0_qspiram);

#include "qspi_context.h"
#include <zephyr/device.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/types.h>
#include <zephyr/drivers/disk.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/init.h>
#include <zephyr/device.h>
#include <soc.h>
#include "component/qspi.h"
#include "psram.h"

#ifdef CONFIG_QSPI

/* Device run time data */
struct qspi_sam0_data {
	QSPI_CTRLA_Type ctrla;
	QSPI_CTRLB_Type ctrlb;
	QSPI_BAUD_Type baud;
	QSPI_INSTRCTRL_Type instrctrl;
	QSPI_INSTRFRAME_Type instframe;
	QSPI_INSTRADDR_Type addr;
	const struct qspi_sam0_config *config;
	const struct device *dev;
	struct qspi_context ctx;

	struct disk_info info;
#if defined(CONFIG_FLASH_PAGE_LAYOUT)
	struct flash_pages_layout layout;
#endif
	const size_t sector_size;
	const size_t sector_count;
	uint8_t *const buf;
	struct disk_info di;
};

struct qspi_sam0_config {
	Qspi *regs;
	const struct pinctrl_dev_config *pcfg;

	volatile uint32_t *mclk;
	volatile uint32_t *mbase;
	uint32_t mclk_mask;

	uint32_t frequency;

	const struct flash_parameters para;
	const size_t sector_size;
	const size_t sector_count;
	const size_t size;
	uint8_t *const buf;
};

static const struct flash_parameters *psram_get_parameters(const struct device *dev)
{
	const struct qspi_sam0_config *cfg = dev->config;
	return &cfg->para;
}

static int psram_read_access(const struct device *dev, off_t addr, void *dest, size_t size)
{
	const struct qspi_sam0_config *cfg = dev->config;
	/* should be between 0 and flash size */
	if ((addr < 0) || ((addr + size) > cfg->size)) {
		return -EINVAL;
	}
	psmemcpy(dest, cfg->buf + addr, size);
	return 0;
}

static int psram_write_access(const struct device *dev, off_t addr, const void *src, size_t size)
{
	const struct qspi_sam0_config *cfg = dev->config;
	/* should be between 0 and flash size */
	if ((addr < 0) || ((addr + size) > cfg->size)) {
		return -EINVAL;
	}
	psmemcpy(cfg->buf + addr, src, size);
	return 0;
}

static void psram_page_layout(const struct device *dev, const struct flash_pages_layout **layout,
			      size_t *layout_size)
{
	struct qspi_sam0_data *data = dev->data;
	*layout = &data->layout;
	*layout_size = 1;
}

static int psram_erase_access(const struct device *dev, off_t offset, size_t size)
{
	return 0;
}

static int psram_size_access(const struct device *dev, uint64_t *size)
{
	const struct qspi_sam0_config *cfg = dev->config;
	*size = cfg->size;
	return 0;
}

static struct qspi_sam0_data *psram;

static int qspi_sam0_instruction_send_quad(struct qspi_sam0_data *data)
{
	/*	const struct qspi_sam0_config *cfg = psram.dev->config; */
	Qspi *regs = data->config->regs;

	regs->CTRLA = data->ctrla;
	regs->INSTRADDR = data->addr;
	regs->INSTRCTRL = data->instrctrl;
	regs->INSTRFRAME = data->instframe;

	/* read back for synchronistaion AHB and APB */
	data->instframe = regs->INSTRFRAME;
	return 0;
}

static int psram_end_transaction(void)
{
	Qspi *regs = psram->config->regs;

	regs->CTRLA.bit.LASTXFER = 1;
	while (!regs->INTFLAG.bit.INSTREND) {
		k_yield();
	}

	while (!regs->INTFLAG.bit.CSRISE) {
		k_yield();
	}

	regs->INTFLAG.reg = QSPI_INTFLAG_INSTREND;
	regs->CTRLA.bit.LASTXFER = 0;
	return 0;
}

static int psram_write(void *des)
{
	psram->instrctrl.reg = QSPI_INSTRCTRL_INSTR(0x38);
	psram->instframe.reg = QSPI_INSTRFRAME_WIDTH_QUAD_CMD | QSPI_INSTRFRAME_INSTREN |
			       QSPI_INSTRFRAME_ADDREN | QSPI_INSTRFRAME_DATAEN |
			       QSPI_INSTRFRAME_ADDRLEN_24BITS |
			       QSPI_INSTRFRAME_TFRTYPE_WRITEMEMORY | QSPI_INSTRFRAME_DUMMYLEN(0);

	psram->addr.reg = (uint32_t)des - 0x04000000;
	return qspi_sam0_instruction_send_quad(psram);
}

#undef CACHE_INVALIDATE
#if defined(CACHE_INVALIDATE)
#include "component/cmcc.h"
#define CMCC_BASE 0x41006000
#endif
static int psram_read(const void *src)
{
	psram->instrctrl.reg = QSPI_INSTRCTRL_INSTR(0xEB);
	psram->instframe.reg = QSPI_INSTRFRAME_WIDTH_QUAD_CMD | QSPI_INSTRFRAME_INSTREN |
			       QSPI_INSTRFRAME_ADDREN | QSPI_INSTRFRAME_DATAEN |
			       QSPI_INSTRFRAME_ADDRLEN_24BITS | QSPI_INSTRFRAME_TFRTYPE_READMEMORY |
			       QSPI_INSTRFRAME_DUMMYLEN(6);

	psram->addr.reg = (uint32_t)src - 0x04000000;
#if defined(CACHE_INVALIDATE)
	Cmcc *creg = (Cmcc *)CMCC_BASE;
	creg->CTRL.bit.CEN = 0;
	while (creg->SR.bit.CSTS) {
		k_yield();
	}
	creg->MAINT0.bit.INVALL = 1;
	creg->CTRL.bit.CEN = 1;
#endif

	return qspi_sam0_instruction_send_quad(psram);
}

static bool is_psram_adr(const void *p)
{
	if (p < (void *)0x04000000) {
		return false;
	}
	if (p > (void *)0x05000000) {
		return false;
	}
	return true;
}

void *psmemset(void *s, int c, size_t size)
{

	const struct qspi_sam0_config *cfg = psram->config;

	if (!is_psram_adr(s)) {
		/* no transfer without buffer */
		return NULL;
	}

	qspi_context_lock(&psram->ctx, false, NULL, NULL, cfg);
	k_mutex_lock(&psram->ctx.psram_mem, K_FOREVER);

	psram_write(s);
	memset(s, 0, size);
	psram_end_transaction();
	k_mutex_unlock(&psram->ctx.psram_mem);

	qspi_context_release(&psram->ctx, 0);
	return s;
}

void *psmemcpy(void *dest, const void *src, size_t size)
{
	void *rc;

	const struct qspi_sam0_config *cfg = psram->config;

	if (is_psram_adr(dest) && is_psram_adr(src)) {
		/* no transfer without buffer */
		return NULL;
	}

	qspi_context_lock(&psram->ctx, false, NULL, NULL, cfg);

	k_mutex_lock(&psram->ctx.psram_mem, K_FOREVER);

	if (is_psram_adr(dest)) {
		psram_write(dest);
		rc = memcpy(dest, src, size);
		psram_end_transaction();
	} else if (is_psram_adr(src)) {
		psram_read(src);
		rc = memcpy(dest, src, size);
		psram_end_transaction();

	} else {
		rc = memcpy(dest, src, size);
	}

	k_mutex_unlock(&psram->ctx.psram_mem);

	qspi_context_release(&psram->ctx, 0);
	return rc;
}

static int qspi_sam0_switch_quad_mode(struct qspi_sam0_data *data)
{
	Qspi *regs = data->config->regs;
	uint32_t dummy;

	regs->TXDATA.reg = 0x35;

	while (!regs->INTFLAG.bit.DRE) {
		k_yield();
	}
	/* dummy read */

	while (regs->INTFLAG.bit.RXC) {
		dummy = regs->RXDATA.reg;
	}

	while (!regs->INTFLAG.bit.CSRISE) {
		k_yield();
	}

	regs->INTFLAG.reg = QSPI_INTENCLR_MASK;

	data->ctrlb.reg =
		QSPI_CTRLB_MODE_MEMORY | QSPI_CTRLB_DATALEN_8BITS | QSPI_CTRLB_CSMODE_NORELOAD;
	regs->CTRLB = data->ctrlb;

	return 0;
}

static int qspi_sam0_configure(const struct device *dev, const struct qspi_sam0_config *config)
{
	const struct qspi_sam0_config *cfg = dev->config;
	Qspi *regs = cfg->regs;

	int div;

	if (qspi_context_configured(&psram->ctx, config)) {
		return 0;
	}

	psram->ctrla.reg = QSPI_CTRLA_ENABLE;
	psram->ctrlb.reg =
		QSPI_CTRLB_MODE_SPI | QSPI_CTRLB_DATALEN_8BITS | QSPI_CTRLB_CSMODE_NORELOAD;

	/* Use the requested or next highest possible frequency */
	div = SOC_ATMEL_SAM0_GCLK0_FREQ_HZ / config->frequency;
	div = CLAMP(div, 0, UINT8_MAX);
	psram->baud.bit.BAUD = div;

	/* Update the configuration only if it has changed */
	if (regs->CTRLA.reg != psram->ctrla.reg || regs->CTRLB.reg != psram->ctrlb.reg ||
	    regs->BAUD.reg != div

	) {
		regs->CTRLA.bit.ENABLE = 0;
		regs->CTRLB = psram->ctrlb;
		regs->BAUD = psram->baud;
		regs->CTRLA = psram->ctrla;
	}

	psram->ctx.config = config;
	psram->dev = dev;
	psram->config = config;

	qspi_sam0_switch_quad_mode(psram);

	return 0;
}

#define MCLK_BASE DT_REG_ADDR(DT_NODELABEL(mclk))

static int qspi_sam0_init(const struct device *dev)
{
	int err;
	const struct qspi_sam0_config *cfg = dev->config;
	psram = dev->data;
	Qspi *regs = cfg->regs;
	Mclk *mreg = (Mclk *)MCLK_BASE;

	mreg->AHBMASK.bit.QSPI_2X_ = 0;
	mreg->AHBMASK.bit.QSPI_ = 1;
	mreg->APBCMASK.bit.QSPI_ = 1;

	/* Ensure all registers are at their default values */
	regs->CTRLA.bit.SWRST = 1;

	/* Disable all SPI interrupts */
	regs->INTENCLR.reg = QSPI_INTENCLR_MASK;

	err = pinctrl_apply_state(cfg->pcfg, PINCTRL_STATE_DEFAULT);
	if (err < 0) {
		return err;
	}

	k_mutex_init(&psram->ctx.psram_mem);

	qspi_context_unlock_unconditionally(&psram->ctx);

	qspi_sam0_configure(dev, cfg);

#if defined(CONFIG_FLASH_PAGE_LAYOUT)
	psram->layout.pages_count = cfg->sector_count;
	psram->layout.pages_size = cfg->sector_size;
#endif

	return 0;
}

static DEVICE_API(flash, qspi_sam0_driver_api) = {
	.get_parameters = psram_get_parameters,
	.read = psram_read_access,
	.write = psram_write_access,
	.erase = psram_erase_access,
	.get_size = psram_size_access,
#if defined(CONFIG_FLASH_PAGE_LAYOUT)
	.page_layout = psram_page_layout,
#endif
};

#define DT_FREQUENCY(node_id)   DT_CAT(node_id, _P_frequency)
#define DT_INST_FREQUENCY(inst) DT_FREQUENCY(DT_DRV_INST(inst))

#define QSPI_SAM0_DEFINE_CONFIG(n)                                                                 \
	static const struct qspi_sam0_config qspi_sam0_config_##n = {                              \
		.regs = (Qspi *)DT_INST_REG_ADDR(n),                                               \
		.mclk = ATMEL_SAM0_DT_INST_MCLK_PM_REG_ADDR_OFFSET(n),                             \
		.mclk_mask = ATMEL_SAM0_DT_INST_MCLK_PM_PERIPH_MASK(n, bit),                       \
		.pcfg = PINCTRL_DT_INST_DEV_CONFIG_GET(n),                                         \
		.frequency = DT_INST_FREQUENCY(n),                                                 \
		.para = {.write_block_size = 512,                                                  \
			 .erase_value = 0xff,                                                      \
			 .caps.no_explicit_erase = true},                                          \
		.sector_size = 512,                                                                \
		.sector_count = 1024 * 1024 * 8 / 512,                                             \
		.size = 1024 * 1024 * 8,                                                           \
		.buf = UINT_TO_POINTER(0x04000000),                                                \
	}

#define QSPI_SAM0_DEVICE_INIT(n)                                                                   \
	PINCTRL_DT_INST_DEFINE(n);                                                                 \
	QSPI_SAM0_DEFINE_CONFIG(n);                                                                \
	static struct qspi_sam0_data qspi_sam0_dev_data_##n = {                                    \
		QSPI_CONTEXT_INIT_LOCK(qspi_sam0_dev_data_##n, ctx),                               \
		QSPI_CONTEXT_INIT_SYNC(qspi_sam0_dev_data_##n, ctx),                               \
	};                                                                                         \
	DEVICE_DT_INST_DEFINE(n, &qspi_sam0_init, NULL, &qspi_sam0_dev_data_##n,                   \
			      &qspi_sam0_config_##n, POST_KERNEL, CONFIG_QSPI_INIT_PRIORITY,       \
			      &qspi_sam0_driver_api);

DT_INST_FOREACH_STATUS_OKAY(QSPI_SAM0_DEVICE_INIT)

#endif
