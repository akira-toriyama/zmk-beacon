/*
 * Copyright (c) 2024 carrefinho
 * Copyright (c) 2025 Prospector ZMK Module Contributors
 * Copyright (c) 2026 akira-toriyama
 *
 * SPDX-License-Identifier: MIT
 *
 * The PWM backlight (backlight.h). It is lit before LVGL and ZMK's display
 * start (SYS_INIT APPLICATION 50; LVGL's glue is APPLICATION 90 and ZMK's
 * display starts from main()), so a screen that never draws still lights up
 * instead of looking dead. The panel driver itself is earlier (POST_KERNEL)
 * and leaves the panel blanked until ZMK unblanks it. Derived from
 * prospector-zmk-module v2.2.3
 * boards/shields/prospector_scanner/src/backlight_init.c; the brightness comes
 * from Kconfig instead of a constant.
 */

#include <zephyr/device.h>
#include <zephyr/drivers/led.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "backlight.h"

LOG_MODULE_REGISTER(beacon_backlight, LOG_LEVEL_INF);

#if DT_HAS_COMPAT_STATUS_OKAY(pwm_leds)

static const struct device *const backlight = DEVICE_DT_GET(DT_COMPAT_GET_ANY_STATUS_OKAY(pwm_leds));

int beacon_backlight_set(uint8_t percent) {
    return led_set_brightness(backlight, 0, percent);
}

static int backlight_init(void) {
    if (!device_is_ready(backlight)) {
        LOG_ERR("backlight not ready");
        return -ENODEV;
    }

    int ret = beacon_backlight_set(CONFIG_BEACON_BACKLIGHT_BRIGHTNESS);
    if (ret < 0) {
        LOG_ERR("backlight brightness: %d", ret);
        return ret;
    }

    return 0;
}

SYS_INIT(backlight_init, APPLICATION, 50);

#else
#error "The prospector shield needs a pwm-leds node for its backlight"
#endif
