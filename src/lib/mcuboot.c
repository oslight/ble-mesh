/*
 * Copyright (c) 2016-2017 Linaro Limited
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define SYS_LOG_DOMAIN "fota/mcuboot"
#define SYS_LOG_LEVEL CONFIG_SYS_LOG_FOTA_LEVEL
#include <logging/sys_log.h>

#include <stddef.h>
#include <errno.h>
#include <string.h>
#include <flash.h>
#include <zephyr.h>
#include <init.h>

#include "mcuboot.h"
#include "product_id.h"

/*
 * Helpers for image trailer, as defined by mcuboot.
 *
 * The first byte of the "image OK" and "copy done" areas contain
 * payload. The rest is padded with 0xff for flash write
 * alignment. The payload is cleared when set to the pad value, and
 * set when set to 0x01.
 */

#define PAD_SIZE 7
#define PAD_VALUE 0xff
#define MAGIC_SIZE 16

__packed
struct boot_copy_done {
	u8_t copy_done;
	u8_t pad[PAD_SIZE];
};

__packed
struct boot_image_ok {
	u8_t image_ok;
	u8_t pad[PAD_SIZE];
};

__packed
struct boot_trailer {
	struct boot_copy_done cd;
	struct boot_image_ok ok;
	u8_t magic[MAGIC_SIZE];
};

static u32_t boot_trailer(u32_t bank_offset)
{
	return bank_offset + FLASH_BANK_SIZE - sizeof(struct boot_trailer);
}

static u32_t boot_trailer_copy_done(u32_t bank_offset)
{
	return boot_trailer(bank_offset) + offsetof(struct boot_trailer, cd);
}

static u32_t boot_trailer_image_ok(u32_t bank_offset)
{
	return boot_trailer(bank_offset) + offsetof(struct boot_trailer, ok);
}

u8_t boot_status_read(void)
{
	u32_t offset;
	u8_t img_ok = 0;

	offset = boot_trailer_image_ok(FLASH_AREA_IMAGE_0_OFFSET);
	flash_read(flash_dev, offset, &img_ok, sizeof(u8_t));

	return img_ok;
}

void boot_status_update(void)
{
	u32_t offset;
	struct boot_image_ok ok;

	offset = boot_trailer_image_ok(FLASH_AREA_IMAGE_0_OFFSET);
	flash_read(flash_dev, offset, &ok.image_ok, sizeof(ok.image_ok));
	if (ok.image_ok == BOOT_STATUS_ONGOING) {
		ok.image_ok = BOOT_STATUS_DONE;
		memset(ok.pad, PAD_VALUE, sizeof(ok.pad));

		flash_write_protection_set(flash_dev, false);
		flash_write(flash_dev, offset, &ok, sizeof(ok));
		flash_write_protection_set(flash_dev, true);
	}
}

void boot_trigger_ota(void)
{
	u32_t copy_done_offset, image_ok_offset;
	struct boot_copy_done cd;
	struct boot_image_ok ok;

	copy_done_offset = boot_trailer_copy_done(FLASH_AREA_IMAGE_1_OFFSET);
	image_ok_offset = boot_trailer_image_ok(FLASH_AREA_IMAGE_1_OFFSET);
	memset(&cd, PAD_VALUE, sizeof(cd));
	memset(&ok, PAD_VALUE, sizeof(ok));

	flash_write_protection_set(flash_dev, false);
	flash_write(flash_dev, copy_done_offset, &cd, sizeof(cd));
	flash_write_protection_set(flash_dev, true);

	flash_write_protection_set(flash_dev, false);
	flash_write(flash_dev, image_ok_offset, &ok, sizeof(ok));
	flash_write_protection_set(flash_dev, true);
}

int boot_erase_flash_bank(u32_t bank_offset)
{
	int ret;

	flash_write_protection_set(flash_dev, false);
	ret = flash_erase(flash_dev, bank_offset, FLASH_BANK_SIZE);
	flash_write_protection_set(flash_dev, true);

	return ret;
}

static int boot_init(struct device *dev)
{
	ARG_UNUSED(dev);
	flash_dev = device_get_binding(FLASH_DRIVER_NAME);
	if (!flash_dev) {
		SYS_LOG_ERR("Failed to find the flash driver");
		return -ENODEV;
	}
	return 0;
}

SYS_INIT(boot_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
