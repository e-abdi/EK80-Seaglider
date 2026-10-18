#include <zephyr/device.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/watchdog.h>
#include <zephyr/logging/log.h>

#include "core/board_io.h"

LOG_MODULE_DECLARE(ek80, CONFIG_EK80_LOG_LEVEL);

#define USER_NODE DT_PATH(zephyr_user)

static const struct gpio_dt_spec coil[] = {
	[RELAY_COIL_EK80_ON] = GPIO_DT_SPEC_GET(USER_NODE, ek80_on_gpios),
	[RELAY_COIL_EK80_OFF] = GPIO_DT_SPEC_GET(USER_NODE, ek80_off_gpios),
};
static const struct gpio_dt_spec bms_pres = GPIO_DT_SPEC_GET(USER_NODE, bms_pres_gpios);
static const struct adc_dt_spec vbat = ADC_DT_SPEC_GET(USER_NODE);

#if CONFIG_EK80_WATCHDOG_TIMEOUT_MS > 0
static const struct device *const wdt = DEVICE_DT_GET(DT_ALIAS(watchdog0));
static int wdt_channel = -1;
#endif

int board_io_init(void)
{
	int rc = 0;

	/* Coils first: the MOSFET gates have pull-downs, keep them off */
	for (size_t i = 0; i < ARRAY_SIZE(coil); i++) {
		if (!gpio_is_ready_dt(&coil[i]) ||
		    gpio_pin_configure_dt(&coil[i], GPIO_OUTPUT_INACTIVE) != 0) {
			LOG_ERR("relay coil GPIO %u not available", (unsigned int)i);
			rc = -EIO;
		}
	}
	/* The bq77915 PRES line is held low through JP2 */
	if (!gpio_is_ready_dt(&bms_pres) ||
	    gpio_pin_configure_dt(&bms_pres, GPIO_OUTPUT_INACTIVE) != 0) {
		LOG_ERR("PRES GPIO not available");
		rc = -EIO;
	}
	if (!adc_is_ready_dt(&vbat) || adc_channel_setup_dt(&vbat) != 0) {
		LOG_ERR("pack voltage ADC not available");
		rc = -EIO;
	}

#if CONFIG_EK80_WATCHDOG_TIMEOUT_MS > 0
	struct wdt_timeout_cfg cfg = {
		.window.max = CONFIG_EK80_WATCHDOG_TIMEOUT_MS,
		.flags = WDT_FLAG_RESET_SOC,
	};

	if (!device_is_ready(wdt)) {
		LOG_ERR("watchdog not ready");
		return -ENODEV;
	}
	wdt_channel = wdt_install_timeout(wdt, &cfg);
	if (wdt_channel < 0 || wdt_setup(wdt, WDT_OPT_PAUSE_HALTED_BY_DBG) != 0) {
		LOG_ERR("watchdog setup failed (%d)", wdt_channel);
		wdt_channel = -1;
		return -EIO;
	}
#endif
	return rc;
}

void relay_coil_set(enum relay_coil c, bool energized)
{
	/* Never drive both coils of the latching relay at once */
	if (energized) {
		gpio_pin_set_dt(&coil[c == RELAY_COIL_EK80_ON ? RELAY_COIL_EK80_OFF
							       : RELAY_COIL_EK80_ON],
				0);
	}
	gpio_pin_set_dt(&coil[c], energized ? 1 : 0);
	LOG_INF("relay %s coil %s", c == RELAY_COIL_EK80_ON ? "EK80-on" : "EK80-off",
		energized ? "energized" : "released");
}

int vbat_read_cv(void)
{
	int16_t sample;
	int32_t sum = 0;
	struct adc_sequence seq = {
		.buffer = &sample,
		.buffer_size = sizeof(sample),
	};

	if (adc_sequence_init_dt(&vbat, &seq) != 0) {
		return -1;
	}
	for (int i = 0; i < CONFIG_EK80_VBAT_SAMPLES; i++) {
		if (adc_read_dt(&vbat, &seq) != 0) {
			LOG_ERR("pack voltage read failed");
			return -1;
		}
		sum += sample;
	}

	int32_t mv = sum / CONFIG_EK80_VBAT_SAMPLES;

	if (adc_raw_to_millivolts_dt(&vbat, &mv) != 0) {
		return -1;
	}
	/* pin mV -> pack mV -> calibrated cV */
	int64_t pack_mv = (int64_t)mv * CONFIG_EK80_VBAT_DIVIDER_X1000 / 1000;

	return (int)(pack_mv * CONFIG_EK80_VBAT_CAL_PERMILLE / 1000 / 10);
}

void board_watchdog_feed(void)
{
#if CONFIG_EK80_WATCHDOG_TIMEOUT_MS > 0
	if (wdt_channel >= 0) {
		wdt_feed(wdt, wdt_channel);
	}
#endif
}
