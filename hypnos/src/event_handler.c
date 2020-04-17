/*
 * Copyright (c) 2020 Endian Technologies AB
 *
 * SPDX-License-Identifier: MPL-2.0
 */

#include <zephyr.h>
#include <drivers/gpio.h>
#include <drivers/sensor.h>
#include <stdbool.h>
#include <lvgl.h>
#include "backlight.h"
#include "battery.h"
#include "clock.h"
#include "event_handler.h"
#include "log.h"
#include "cts_sync.h"

/* ********** defines ********** */
#define BAT_PERCENTAGE_READ_INTERVAL K_MINUTES(5)
#define BAT_CHA 12
#define BTN_PORT DT_ALIAS_SW0_GPIOS_CONTROLLER
#define BTN_IN  DT_ALIAS_SW0_GPIOS_PIN
#define BTN_OUT 15
#define EDGE    (GPIO_INT_EDGE | GPIO_INT_DOUBLE_EDGE)
#define PULL_UP DT_ALIAS_SW0_GPIOS_FLAGS
#define TOUCH_PORT DT_INST_0_HYNITRON_CST816S_LABEL
#define BACKLIGHT_TIMEOUT K_SECONDS(5)
#define BT_TIMEOUT K_SECONDS(30)
/* ********** defines ********** */

/* ********** variables ********** */
static struct k_timer battery_percentage_timer;
static struct k_timer clock_tick_timer;
static struct k_timer backlight_off_timer;
static struct k_timer bt_off_timer;
static struct device *charging_dev;
static struct gpio_callback charging_cb;
static struct device *button_dev;
static struct gpio_callback button_cb;
static struct device *touch_dev;
static struct sensor_trigger tap = {
	.type = SENSOR_TRIG_DATA_READY,
	.chan = SENSOR_CHAN_ACCEL_XYZ,
};
bool bt_enabled = 0;
static lv_obj_t *bt_label;

/* ********** variables ********** */

/* ********** init function ********** */
void event_handler_init()
{
        // TODO: Check return values for error handling.

	/* Initialize GPIOs */
        charging_dev = device_get_binding("GPIO_0");
        gpio_pin_configure(charging_dev, BAT_CHA, GPIO_DIR_IN | GPIO_INT
                | EDGE);
        gpio_init_callback(&charging_cb, battery_charging_isr,
                BIT(BAT_CHA));
	button_dev = device_get_binding(BTN_PORT);
	gpio_pin_configure(button_dev, BTN_IN, GPIO_DIR_IN | GPIO_INT |  PULL_UP
			   | EDGE);
	gpio_init_callback(&button_cb, button_pressed_isr, BIT(BTN_IN));
	touch_dev = device_get_binding(TOUCH_PORT);

	/* Enable GPIOs */
	gpio_add_callback(charging_dev, &charging_cb);
        gpio_pin_enable_callback(charging_dev, BAT_CHA);
	gpio_add_callback(button_dev, &button_cb);
	gpio_pin_enable_callback(button_dev, BTN_IN);
	sensor_trigger_set(touch_dev, &tap, touch_tap_isr);

	/* Set button out pin to high to enable the button */
	u32_t button_out = 1U;
        gpio_pin_configure(button_dev, BTN_OUT, GPIO_DIR_OUT);
        gpio_pin_write(button_dev, BTN_OUT, button_out);

	/* Initialize timers */
        k_timer_init(&battery_percentage_timer,
		     battery_percentage_isr, NULL);
	k_timer_init(&clock_tick_timer, clock_tick_isr, NULL);
	k_timer_init(&backlight_off_timer, backlight_off_isr, NULL);
	k_timer_init(&bt_off_timer, bt_off_isr, NULL);

	/* Start timers */
        k_timer_start(&battery_percentage_timer, BAT_PERCENTAGE_READ_INTERVAL,
		      BAT_PERCENTAGE_READ_INTERVAL);
	k_timer_start(&clock_tick_timer, K_SECONDS(1), K_SECONDS(1));
	k_timer_start(&backlight_off_timer, BACKLIGHT_TIMEOUT, 0);

	/* Special cases */
        /* Get battery charging status */
	k_sleep(10);
        u32_t res = 0U;
        gpio_pin_read(charging_dev, BAT_CHA, &res);
        battery_update_charging_status(res != 1U);

	LOG_DBG("Event handler init: Done");
}
/* ********** init function ********** */

/* ********** handler functions ********** */
void backlight_off_isr(struct k_timer *light_off)
{
	backlight_enable(false);
}

void battery_percentage_isr(struct k_timer *bat)
{
        battery_update_percentage();
}

void battery_charging_isr(struct device *gpiobat, struct gpio_callback *cb, u32_t pins)
{
        u32_t res = 0U;
        gpio_pin_read(charging_dev, 12, &res);
        battery_update_charging_status(res != 1U);
	backlight_enable(true);
	k_timer_start(&backlight_off_timer, BACKLIGHT_TIMEOUT, 0);
}

void button_pressed_isr(struct device *gpiobtn, struct gpio_callback *cb, u32_t pins)
{
	backlight_enable(true);
	k_timer_start(&backlight_off_timer, BACKLIGHT_TIMEOUT, 0);
	bt_enabled = true;
	lv_label_set_text(bt_label, LV_SYMBOL_BLUETOOTH);
	LOG_INF("Bluetooth mode enabled...");
	k_timer_start(&bt_off_timer, BT_TIMEOUT, 0);
}

void clock_tick_isr(struct k_timer *tick)
{
	clock_increment_local_time();
	clock_print_time();
	battery_print_status();
}

void touch_tap_isr(struct device *touch_dev, struct sensor_trigger *tap)
{
	backlight_enable(true);
	k_timer_start(&backlight_off_timer, BACKLIGHT_TIMEOUT, 0);
}

void bt_off_isr(struct k_timer *bt)
{
	bt_enabled = 0;
	lv_label_set_text(bt_label, "");
	LOG_INF("Bluetooth mode disabled...");
}

bool bt_mode(void)
{
	return bt_enabled;
}

void bt_gfx_init()
{
	bt_label = lv_label_create(lv_scr_act(), NULL);
	lv_obj_align(bt_label, NULL, LV_ALIGN_IN_TOP_LEFT, 0, 0);
	lv_label_set_text(bt_label, "");
}
/* ********** handler functions ********** */