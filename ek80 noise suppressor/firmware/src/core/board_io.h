/* Board-level I/O other than the serial ports. */
#pragma once

#include <stdbool.h>

/*
 * K1 is a dual-coil latching relay. Pulsing one coil connects the pack to
 * the EK80; pulsing the other connects the pack to the charger (EK80 off).
 */
enum relay_coil {
	RELAY_COIL_EK80_ON,
	RELAY_COIL_EK80_OFF,
};

int board_io_init(void);
void relay_coil_set(enum relay_coil coil, bool energized);
/* Pack voltage in centivolts (the unit the glider expects), or -1. */
int vbat_read_cv(void);
void board_watchdog_feed(void);
