/*
 * Copyright (c) 2016-2017 Linaro Limited
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define SYS_LOG_DOMAIN "fota/main"
#define SYS_LOG_LEVEL CONFIG_SYS_LOG_FOTA_LEVEL
#include <logging/sys_log.h>

#include <zephyr.h>
#include <board.h>
#include <gpio.h>
#include <pwm.h>
#include <tc_util.h>

#include <bluetooth/bluetooth.h>
#include <bluetooth/mesh.h>

/* Local helpers and functions */
#include "tstamp_log.h"
#include "app_work_queue.h"
#include "mcuboot.h"
#include "product_id.h"

#define REPLY_EXP_RANGE 50

/* Force CID from Nordic as our main use cases are nRF5-based devices */
#define CID_NORDIC 0x0059

struct device *flash_dev;

/* Blink */
struct pwm_blink_ctx {
	struct k_delayed_work work;
	u32_t delay;
	u32_t count;
};

struct gpio_blink_ctx {
	struct k_delayed_work work;
	struct device *gpio;
	u32_t gpio_pin;
	u32_t count;
};

static struct pwm_blink_ctx pwm_blink_context;
static struct gpio_blink_ctx prov_blink_context;

#define PROV_BLINK_DELAY	K_SECONDS(1)

/* PWM */

/* 100 is more than enough for it to be flicker free */
#define PWM_PERIOD (USEC_PER_SEC / 100)

static struct device *pwm_white;
static u8_t white_current;

static u32_t scale_pulse(u8_t level, u8_t ceiling)
{
	if (level && ceiling) {
		/* Scale level based on ceiling and return period */
		return PWM_PERIOD / 255 * level * ceiling / 255;
	}

	return 0;
}

static int write_pwm_pin(struct device *pwm_dev, u32_t pwm_pin,
			 u8_t level, u8_t ceiling)
{
	u32_t pulse = scale_pulse(level, ceiling);

	SYS_LOG_DBG("Set PWM %d: level %d, ceiling %d, pulse %lu",
				pwm_pin, level, ceiling, pulse);

	return pwm_pin_set_usec(pwm_dev, pwm_pin, PWM_PERIOD, pulse);
}

static int update_pwm(u8_t dimmer)
{
	u8_t white;
	int ret = 0;

	SYS_LOG_DBG("dimmer: %d", dimmer);

	if (dimmer < 0) {
		dimmer = 0;
	}
	if (dimmer > 100) {
		dimmer = 100;
	}

	white = 255 * dimmer / 100;
	if (white != white_current) {
		white_current = white;
		ret = write_pwm_pin(pwm_white, CONFIG_APP_PWM_WHITE_PIN, white,
					CONFIG_APP_PWM_WHITE_PIN_CEILING);
		if (ret) {
			SYS_LOG_ERR("Failed to update white PWM");
			return ret;
		}
	}

	return ret;
}

static int init_pwm(void)
{
	pwm_white = device_get_binding(CONFIG_APP_PWM_WHITE_DEV);
	if (!pwm_white) {
		SYS_LOG_ERR("Failed to get PWM device used for white");
		return -ENODEV;
	}

	return 0;
}

/* Bluetooth Mesh */

static struct bt_mesh_cfg cfg_srv = {
#if defined(CONFIG_BT_MESH_RELAY)
	.relay = BT_MESH_RELAY_ENABLED,
#else
	.relay = BT_MESH_RELAY_NOT_SUPPORTED,
#endif
	.beacon = BT_MESH_BEACON_ENABLED,
	.frnd = BT_MESH_FRIEND_NOT_SUPPORTED,
#if defined(CONFIG_BT_MESH_GATT_PROXY)
	.gatt_proxy = BT_MESH_GATT_PROXY_ENABLED,
#else
	.gatt_proxy = BT_MESH_GATT_PROXY_NOT_SUPPORTED,
#endif
	/*
	 * Here also used when comparing with received msgs, even if
	 * not ideal (as the only way to know the original TTL is via
	 * heartbeat messages).
	 */
	.default_ttl = 5,

	/* 3 transmissions with 20ms interval */
	.net_transmit = BT_MESH_TRANSMIT(2, 20),
	.relay_retransmit = BT_MESH_TRANSMIT(2, 20),
};

static struct bt_mesh_health health_srv = {
};

static struct bt_mesh_model_pub gen_onoff_pub;

static u8_t onoff_state;

static void gen_onoff_reply_status(struct bt_mesh_model *model,
				   struct bt_mesh_msg_ctx *src_ctx)
{
	/* 2 for msg_init, 4 for OnOff Status and 4 for TransMIC */
	struct net_buf_simple *msg = NET_BUF_SIMPLE(2 + 4 + 4);
	struct bt_mesh_msg_ctx ctx;
	int exp_ms;

	/* Reuse source ctx (addr is already the right remote) */
	memcpy(&ctx, src_ctx, sizeof(ctx));
	ctx.send_ttl = cfg_srv.default_ttl;

	/* Generic OnOff Status */
	bt_mesh_model_msg_init(msg, BT_MESH_MODEL_OP_2(0x82, 0x04));
	net_buf_simple_add_u8(msg, onoff_state);
	SYS_LOG_DBG("Sending OnOff Status (state: %d)", onoff_state);

	/*
	 * HACK: add delay to avoid overloading network. Suggestion
	 * from spec is to use something between 20 to 50 ms / 200 ms.
	 * TODO: change to be handled by a worker instead
	 */
	exp_ms = (REPLY_EXP_RANGE / 2) + sys_rand32_get() % REPLY_EXP_RANGE;
	k_sleep(exp_ms);

	SYS_LOG_DBG("Remote Address: %x, Send TTL: %d",
				ctx.addr, ctx.send_ttl);
	if (bt_mesh_model_send(model, &ctx, msg, NULL, NULL)) {
		SYS_LOG_ERR("Unable to send Generic OnOff Status message");
	}
}

static void gen_onoff_get(struct bt_mesh_model *model,
			  struct bt_mesh_msg_ctx *ctx,
			  struct net_buf_simple *buf)
{
	gen_onoff_reply_status(model, ctx);
}

static int onoff_set(struct bt_mesh_model *model,
		     struct bt_mesh_msg_ctx *ctx,
		     struct net_buf_simple *buf)
{
	u8_t onoff;
	u8_t tid;
	int delay = 0;

	SYS_LOG_DBG("recv_ttl: %d", ctx->recv_ttl);

	/* TODO: Transition Time and Delay */
	onoff = net_buf_simple_pull_u8(buf);
	tid = net_buf_simple_pull_u8(buf);
	SYS_LOG_DBG("onoff: %d, tid: %d", onoff, tid);

	if (onoff != onoff_state) {
		onoff_state = onoff;
		SYS_LOG_DBG("Internal light state changed to %d", onoff_state);

		if (onoff_state) {
			/* Use TTL to define the blink delay */
			/* TODO: Fetch TTL from the sender */
			delay = cfg_srv.default_ttl - ctx->recv_ttl;
			if (delay < 0) {
				delay = 0;
			}
		}

		/* Remove possible pending previous work */
		k_delayed_work_cancel(&pwm_blink_context.work);

		SYS_LOG_DBG("Set Blink delay to %d seconds", delay);
		pwm_blink_context.count = 1;
		pwm_blink_context.delay = 50 * (delay * 4);

		/* TODO: Use model delay instead of 10 ms */
		k_delayed_work_submit(&pwm_blink_context.work, 10);

		return 1;
	}

	return 0;
}

static void gen_onoff_set(struct bt_mesh_model *model,
			  struct bt_mesh_msg_ctx *ctx,
			  struct net_buf_simple *buf)
{
	if (onoff_set(model, ctx, buf)) {
		/* Send back ACK/Status */
		gen_onoff_reply_status(model, ctx);
	}
}

static void gen_onoff_set_unack(struct bt_mesh_model *model,
				struct bt_mesh_msg_ctx *ctx,
				struct net_buf_simple *buf)
{
	onoff_set(model, ctx, buf);
}

static const struct bt_mesh_model_op gen_onoff_op[] = {
	{ BT_MESH_MODEL_OP_2(0x82, 0x01), 0, gen_onoff_get },
	{ BT_MESH_MODEL_OP_2(0x82, 0x02), 2, gen_onoff_set },
	{ BT_MESH_MODEL_OP_2(0x82, 0x03), 2, gen_onoff_set_unack },
	BT_MESH_MODEL_OP_END,
};

static struct bt_mesh_model root_models[] = {
	BT_MESH_MODEL_CFG_SRV(&cfg_srv),
	BT_MESH_MODEL_HEALTH_SRV(&health_srv),
	BT_MESH_MODEL(BT_MESH_MODEL_ID_GEN_ONOFF_SRV, gen_onoff_op,
		      &gen_onoff_pub, NULL),
};

static struct bt_mesh_model vnd_models[] = {
};

static struct bt_mesh_elem elements[] = {
	BT_MESH_ELEM(0, root_models, vnd_models),
};

static const struct bt_mesh_comp comp = {
	.cid = CID_NORDIC,
	.elem = elements,
	.elem_count = ARRAY_SIZE(elements),
};

static void prov_complete(void)
{
	SYS_LOG_INF("Provisioning completed!");

#if defined(BT_GPIO_PIN) && defined(BT_GPIO_PORT)
	prov_blink_context.gpio = device_get_binding(BT_GPIO_PORT);
	prov_blink_context.gpio_pin = BT_GPIO_PIN;
#elif defined(LED0_GPIO_PIN) && defined(LED0_GPIO_PORT)
	/* Use LED0 in case there is no dedicated LED for BT */
	prov_blink_context.gpio = device_get_binding(LED0_GPIO_PORT);
	prov_blink_context.gpio_pin = LED0_GPIO_PIN;
#endif

	if (prov_blink_context.gpio) {
		gpio_pin_configure(prov_blink_context.gpio,
				   prov_blink_context.gpio_pin,
				   GPIO_DIR_OUT);

		app_wq_submit_delayed(&prov_blink_context.work,
				      PROV_BLINK_DELAY);
	}
}

static uint8_t dev_uuid[16];

/* TODO: support either blink or number by default */
static const struct bt_mesh_prov prov = {
	.uuid = dev_uuid,
	.output_actions = BT_MESH_NO_OUTPUT,
	.complete = prov_complete,
};

static void bt_ready(int err)
{
	u32_t id_number;
	int i;

	if (err) {
		SYS_LOG_ERR("Bluetooth init failed (err %d)", err);
		return;
	}

	SYS_LOG_INF("Bluetooth initialized");

	/* Set Device UUID based on the product id number */
	id_number = product_id_get()->number;
	for (i = 0; i < sizeof(dev_uuid); i++) {
		dev_uuid[i] = id_number >> 8 * i & 0xff;
	}

	err = bt_mesh_init(&prov, &comp);
	if (err) {
		SYS_LOG_ERR("Initializing mesh failed (err %d)", err);
		return;
	}

	SYS_LOG_INF("Mesh initialized");
}

static void pwm_blink_handler(struct k_work *work)
{
	struct pwm_blink_ctx *blink =
			CONTAINER_OF(work, struct pwm_blink_ctx, work);

	if (onoff_state) {
		if (blink->delay) {
			/* Blink depending on the value */
			update_pwm(100 * (blink->count++ % 2));
			k_delayed_work_submit(&blink->work, blink->delay);
		} else {
			update_pwm(100);
		}
	} else {
		/* Just turn it off */
		update_pwm(0);
		blink->delay = 0;
	}
}

static void prov_blink_handler(struct k_work *work)
{
	struct gpio_blink_ctx *blink =
		CONTAINER_OF(work, struct gpio_blink_ctx, work);

	gpio_pin_write(blink->gpio, blink->gpio_pin, blink->count++ % 2);
	app_wq_submit_delayed(&blink->work, PROV_BLINK_DELAY);
}

void main(void)
{
	int ret;

	tstamp_hook_install();
	app_wq_init();

	/* Blinking pattern handler */
	k_delayed_work_init(&pwm_blink_context.work, pwm_blink_handler);

	/* Visual feedback for provisioning */
	k_delayed_work_init(&prov_blink_context.work, prov_blink_handler);

	SYS_LOG_INF("Bluetooth Mesh Smart Light Bulb");
	SYS_LOG_INF("Device: %s, Serial: %x",
		    product_id_get()->name, product_id_get()->number);

	TC_PRINT("Initializing PWM\n");
	if (init_pwm()) {
		_TC_END_RESULT(TC_FAIL, "init_pwm");
		TC_END_REPORT(TC_FAIL);
		return;
	}
	_TC_END_RESULT(TC_PASS, "init_pwm");

	TC_PRINT("Initializing Bluetooth Stack\n");
	ret = bt_enable(bt_ready);
	if (ret) {
		SYS_LOG_ERR("Bluetooth init failed (err %d)", ret);
		_TC_END_RESULT(TC_FAIL, "init_bt_enable");
		TC_END_REPORT(TC_FAIL);
		return;
	}
	_TC_END_RESULT(TC_PASS, "init_bt_enable");

	TC_END_REPORT(TC_PASS);

	/*
	 * From this point on, just handle work.
	 */
	app_wq_run();
}
