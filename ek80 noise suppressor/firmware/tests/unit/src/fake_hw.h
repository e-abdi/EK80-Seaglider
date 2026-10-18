/* Test doubles for the serial ports and board I/O, plus scenario helpers. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "core/board_io.h"
#include "core/port.h"

extern int64_t t_now;

struct coil_event {
	enum relay_coil coil;
	bool on;
	int64_t t;
};

void fake_reset(void);
const char *fake_tx(enum ek80_port port);
void fake_tx_clear(void);
int fake_count(enum ek80_port port, const char *needle);
void fake_set_vbat_cv(int cv);

/* Relay coil history and current drive */
int fake_coil_events(const struct coil_event **events);
bool fake_coil_on(enum relay_coil coil);
bool fake_coils_overlapped(void);

/* Scenario helpers: send text into a port, advance time in 10 ms steps */
void glider_says(const char *text);
void ek80_says(const char *text);
void advance_ms(int ms);
