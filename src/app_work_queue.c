/*
 * Copyright (c) 2016 Intel Corporation
 * Copyright (c) 2016 Wind River Systems, Inc.
 * Copyright (c) 2017 Linaro, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * This file copies the work queue implementation from Zephyr, but
 * allows users to just start running the work queue on a desired
 * thread.
 *
 * This is useful to avoid allocating two thread stacks if one thread
 * (in our case, the main thread) isn't using its stack for the
 * application's lifetime, and could be doing useful work instead.
 *
 * TODO: propose a more upstream-friendly way to support this.
 */

#include "app_work_queue.h"

static struct k_work_q app_queue;

struct k_work_q *_app_q = &app_queue;

void app_wq_init(void)
{
	k_queue_init(&_app_q->queue);
}

void app_wq_run(void)
{
	while (1) {
		struct k_work *work;
		k_work_handler_t handler;

		work = k_queue_get(&_app_q->queue, K_FOREVER);

		handler = work->handler;

		/* Reset pending state so it can be resubmitted by handler */
		if (atomic_test_and_clear_bit(work->flags,
					       K_WORK_STATE_PENDING)) {
			handler(work);
		}

		/* Make sure we don't hog up the CPU if the QUEUE never (or
		 * very rarely) gets empty.
		 */
		k_yield();
	}
}
