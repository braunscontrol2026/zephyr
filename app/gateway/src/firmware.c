#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(firmware_c, LOG_LEVEL_DBG);

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/sys/util.h>
#include <zephyr/dfu/flash_img.h>
#include <zephyr/dfu/mcuboot.h>
#include <zephyr/sys/reboot.h>

static struct update_ctx {
	struct flash_img_context upd_ctx;
	off_t pos;
} ctx;

void firmware_update_start(void)
{
	int err;

	err = boot_erase_img_bank(flash_img_get_upload_slot());

	if (err == 0) {
		err = flash_img_init(&ctx.upd_ctx);
	}
	if (err != 0) {
		LOG_ERR("cannot init image for update %d", err);
	}
}

size_t write_firmware_blk(void *data, size_t slen)
{
	return flash_img_buffered_write(&ctx.upd_ctx, data, slen, false) == 0 ? slen : 0;
}

void firmware_update_abort(void)
{
}

void firmware_update_confirm(void)
{
	char dummy = 0;

	flash_img_buffered_write(&ctx.upd_ctx, &dummy, 0, true);
	boot_request_upgrade(BOOT_UPGRADE_PERMANENT);
	LOG_INF("mark image in slot 1 as permanent");
	k_sleep(K_MSEC(1500));
	sys_reboot(SYS_REBOOT_COLD);
}
