/*
 * EK80 noise suppressor - Raspberry Pi Pico firmware
 *
 * A single event loop: wait for UART data or the next controller deadline,
 * feed received bytes to the application core, feed the watchdog.
 */
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "core/app.h"
#include "core/board_io.h"
#include "core/controller.h"
#include "core/port.h"

LOG_MODULE_DECLARE(ek80, CONFIG_EK80_LOG_LEVEL);

#define MAX_WAIT_MS  100
#define STATUS_EVERY 60000

static void log_status(void)
{
	const struct port_stats *g = port_get_stats(PORT_GLIDER);
	const struct port_stats *e = port_get_stats(PORT_EK80);

	LOG_INF("state=%s glider rx=%u drop=%u err=%u, ek80 rx=%u drop=%u err=%u",
		controller_state_name(controller_state()), g->rx_bytes, g->rx_dropped,
		g->rx_errors, e->rx_bytes, e->rx_dropped, e->rx_errors);
}

static void service_port(enum ek80_port port)
{
	uint8_t buf[64];
	size_t n;

	if (port == PORT_EK80) {
		switch (controller_ek80_rx_mode()) {
		case EK80_RX_DISCARD:
			port_discard_rx(port);
			return;
		case EK80_RX_HOLD:
			return;
		case EK80_RX_PROCESS:
			break;
		}
	}
	while ((n = port_read(port, buf, sizeof(buf))) > 0) {
		app_feed(port, buf, n, k_uptime_get());
		if (port == PORT_EK80 && controller_ek80_rx_mode() != EK80_RX_PROCESS) {
			/* The sequence just ended; the rest is discarded next pass */
			break;
		}
	}
}

int main(void)
{
	int64_t next_status = STATUS_EVERY;

	LOG_INF("EK80 noise suppressor starting");

	if (board_io_init() != 0) {
		LOG_ERR("board I/O init failed, continuing");
	}
	if (port_init() != 0) {
		/* Without UARTs nothing can work; let the watchdog retry */
		LOG_ERR("serial ports unavailable");
		for (;;) {
			k_sleep(K_SECONDS(1));
		}
	}

	app_init(k_uptime_get());

	for (;;) {
		int64_t now = k_uptime_get();
		int64_t wait = MIN(app_next_deadline() - now, MAX_WAIT_MS);

		board_watchdog_feed();
		port_wait_ms((int32_t)MAX(wait, 0));

		service_port(PORT_GLIDER);
		service_port(PORT_EK80);

		now = k_uptime_get();
		app_poll(now);

		if (now >= next_status) {
			next_status = now + STATUS_EVERY;
			log_status();
		}
	}
	return 0;
}
