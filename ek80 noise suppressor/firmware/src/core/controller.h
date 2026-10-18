/*
 * EK80 power and command sequence.
 *
 * A dive start from the glider powers the EK80 through the latching relay,
 * sets its clock, puts it to sleep, wakes it and starts the ensemble. A dive
 * stop interrupts the ensemble, puts the EK80 to sleep, switches the relay
 * back to the charger and then answers the glider with "CMD:".
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

enum ctrl_state {
	ST_RESET,         /* off-coil pulse, no reply (boot, 'r', idle stop) */
	ST_OFF,           /* EK80 unpowered, pack on charger */
	ST_POWER_ON,      /* on-coil pulse */
	ST_SEND_TIME,     /* wait for CMD, send the glider's SRTC line */
	ST_SEND_SLEEP,    /* wait for CMD, send SLEEP */
	ST_WAIT_SLEEP,    /* wait for HANDSHAKE */
	ST_WAKE_DELAY,    /* wait, then send wake */
	ST_SEND_RUN,      /* wait for CMD, send RUN ENSEMBLE */
	ST_RUNNING,       /* ensemble running; CMD means it ended */
	ST_WAIT_SHUTDOWN, /* SLEEP sent, wait for HANDSHAKE */
	ST_POWER_OFF,     /* off-coil pulse, then "CMD:" to the glider */
	ST_COUNT,
};

/* What the main loop should do with bytes arriving from the EK80 */
enum ek80_rx_mode {
	EK80_RX_DISCARD, /* EK80 unpowered or shutting down */
	EK80_RX_HOLD,    /* powering up: keep bytes until the sequence starts */
	EK80_RX_PROCESS,
};

struct ek80_status {
	bool ping_ok;
	int start_cv; /* pack voltage at the end of the power-on pulse */
	int end_cv;   /* pack voltage at the last dive stop */
	bool live;    /* echo EK80 lines to the glider */
};

void controller_init(int64_t now_ms);
void controller_poll(int64_t now_ms);
int64_t controller_next_deadline(void);

/* Glider requests */
void controller_dive_start(const char *srtc_line, int64_t now_ms);
void controller_dive_stop(int64_t now_ms);
void controller_reset(int64_t now_ms);
void controller_set_live(bool on);
void controller_set_end_voltage(int cv);

/* A trimmed line from the EK80 */
void controller_on_ek80_line(const char *line, int64_t now_ms);

enum ek80_rx_mode controller_ek80_rx_mode(void);
/* Changes each time the EK80 is powered up; lines never span two sessions */
uint32_t controller_ek80_session(void);
enum ctrl_state controller_state(void);
const char *controller_state_name(enum ctrl_state st);
const struct ek80_status *controller_status(void);
