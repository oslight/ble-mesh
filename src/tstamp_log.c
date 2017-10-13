/*
 * Copyright (c) 2017 Linaro Limited
 * Copyright (c) 2010, 2013-2014 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tstamp_log.h"

/*
 * Even though we don't actually log from this file, without this
 * define, sys_log.h will think it's inactive in this compilation
 * unit.
 */
#define SYS_LOG_LEVEL SYS_LOG_LEVEL_ERROR


#include <logging/sys_log.h>
#include <misc/printk.h>
#include <zephyr.h>

#include <stdarg.h>

static void tstamp_log_fn(const char *fmt, ...)
{
	va_list ap;
	u32_t up_ms = k_uptime_get_32();

	printk("[%07u] ", up_ms);

	va_start(ap, fmt);
	vprintk(fmt, ap);
	va_end(ap);
}

void tstamp_hook_install(void)
{
	syslog_hook_install(tstamp_log_fn);
}
