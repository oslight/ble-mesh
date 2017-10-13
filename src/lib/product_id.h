/*
 * Copyright (c) 2016 Linaro Limited
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef __FOTA_DEVICE_H__
#define __FOTA_DEVICE_H__

struct product_id_t {
	const char *name;
	u32_t number;
};

/**
 * @brief Get a pointer to this device's unique ID.
 *
 * This function is safe to call from the time main() is invoked and
 * afterwards. Before, its return value is unpredictable.
 *
 * @return Pointer to product ID structure.
 */
const struct product_id_t *product_id_get(void);

#endif	/* __FOTA_DEVICE_H__ */
