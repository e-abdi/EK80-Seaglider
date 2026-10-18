#include "core/controller.h"

#include <stdio.h>
#include <string.h>

#include <zephyr/logging/log.h>
#include <zephyr/smf.h>
#include <zephyr/sys/util.h>

#include "core/board_io.h"
#include "core/port.h"

LOG_MODULE_DECLARE(ek80, CONFIG_EK80_LOG_LEVEL);

#define NO_DEADLINE INT64_MAX
#define SRTC_MAX    CONFIG_EK80_GLIDER_LINE_MAX

enum ctrl_ev {
	EV_TIMEOUT,
	EV_DIVE_START,
	EV_DIVE_STOP,
	EV_RESET,
	EV_EK80_CMD,
	EV_EK80_HANDSHAKE,
};

static const struct smf_state states[ST_COUNT + 1];
#define ST_ROOT ST_COUNT

static struct {
	struct smf_ctx smf; /* must be first */
	enum ctrl_ev ev;
	int64_t now;
	int64_t deadline;
	enum ctrl_state cur;
	char srtc[SRTC_MAX];
	uint32_t session;
	struct ek80_status st;
} c;

/* ------------------------------------------------------------------ */
/* Helpers                                                              */
/* ------------------------------------------------------------------ */

static void go(enum ctrl_state st)
{
	LOG_INF("state %s -> %s", controller_state_name(c.cur), controller_state_name(st));
	c.deadline = NO_DEADLINE;
	c.cur = st;
	smf_set_state(SMF_CTX(&c), &states[st]);
}

static void set_deadline(int32_t ms)
{
	c.deadline = c.now + ms;
}

static void ek80_send(const char *s)
{
	LOG_INF("-> EK80: %s", s);
	port_puts(PORT_EK80, s);
}

static void dispatch(enum ctrl_ev ev, int64_t now_ms)
{
	c.now = now_ms;
	c.ev = ev;
	(void)smf_run_state(SMF_CTX(&c));
}

/* ------------------------------------------------------------------ */
/* States                                                               */
/* ------------------------------------------------------------------ */

/* Glider requests that apply in every state unless a state overrides them */
static enum smf_state_result root_run(void *o)
{
	ARG_UNUSED(o);

	switch (c.ev) {
	case EV_DIVE_START:
		go(ST_POWER_ON);
		break;
	case EV_DIVE_STOP:
	case EV_RESET:
		go(ST_RESET);
		break;
	default:
		/* EK80 prompts that no state is waiting for are dropped */
		break;
	}
	return SMF_EVENT_HANDLED;
}

static void reset_entry(void *o)
{
	ARG_UNUSED(o);
	c.st.live = false;
	c.st.ping_ok = false;
	relay_coil_set(RELAY_COIL_EK80_OFF, true);
	set_deadline(CONFIG_EK80_RELAY_PULSE_MS);
}

static void off_coil_exit(void *o)
{
	ARG_UNUSED(o);
	relay_coil_set(RELAY_COIL_EK80_OFF, false);
}

static enum smf_state_result reset_run(void *o)
{
	ARG_UNUSED(o);
	if (c.ev == EV_TIMEOUT) {
		go(ST_OFF);
		return SMF_EVENT_HANDLED;
	}
	return SMF_EVENT_PROPAGATE;
}

static enum smf_state_result off_run(void *o)
{
	ARG_UNUSED(o);
	return SMF_EVENT_PROPAGATE;
}

static void power_on_entry(void *o)
{
	ARG_UNUSED(o);
	relay_coil_set(RELAY_COIL_EK80_ON, true);
	set_deadline(CONFIG_EK80_RELAY_PULSE_MS);
}

static void power_on_exit(void *o)
{
	ARG_UNUSED(o);
	relay_coil_set(RELAY_COIL_EK80_ON, false);
}

static enum smf_state_result power_on_run(void *o)
{
	ARG_UNUSED(o);
	if (c.ev == EV_TIMEOUT) {
		c.st.start_cv = vbat_read_cv();
		c.session++;
		go(ST_SEND_TIME);
		return SMF_EVENT_HANDLED;
	}
	return SMF_EVENT_PROPAGATE;
}

static enum smf_state_result send_time_run(void *o)
{
	ARG_UNUSED(o);
	if (c.ev == EV_EK80_CMD) {
		char line[SRTC_MAX + 2];

		snprintf(line, sizeof(line), "%s\r\n", c.srtc);
		ek80_send(line);
		go(ST_SEND_SLEEP);
		return SMF_EVENT_HANDLED;
	}
	return SMF_EVENT_PROPAGATE;
}

static enum smf_state_result send_sleep_run(void *o)
{
	ARG_UNUSED(o);
	if (c.ev == EV_EK80_CMD) {
		ek80_send("SLEEP\n");
		go(ST_WAIT_SLEEP);
		return SMF_EVENT_HANDLED;
	}
	return SMF_EVENT_PROPAGATE;
}

static enum smf_state_result wait_sleep_run(void *o)
{
	ARG_UNUSED(o);
	if (c.ev == EV_EK80_HANDSHAKE) {
		go(ST_WAKE_DELAY);
		return SMF_EVENT_HANDLED;
	}
	return SMF_EVENT_PROPAGATE;
}

static void wake_delay_entry(void *o)
{
	ARG_UNUSED(o);
	set_deadline(CONFIG_EK80_WAKE_DELAY_MS);
}

static enum smf_state_result wake_delay_run(void *o)
{
	ARG_UNUSED(o);
	if (c.ev == EV_TIMEOUT) {
		ek80_send("wake\n");
		go(ST_SEND_RUN);
		return SMF_EVENT_HANDLED;
	}
	return SMF_EVENT_PROPAGATE;
}

static enum smf_state_result send_run_run(void *o)
{
	ARG_UNUSED(o);
	if (c.ev == EV_EK80_CMD) {
		ek80_send(CONFIG_EK80_RUN_CMD "\n");
		c.st.ping_ok = false;
		go(ST_RUNNING);
		return SMF_EVENT_HANDLED;
	}
	return SMF_EVENT_PROPAGATE;
}

static enum smf_state_result running_run(void *o)
{
	ARG_UNUSED(o);
	switch (c.ev) {
	case EV_DIVE_STOP:
		/* A bare line end interrupts the ensemble; the EK80 answers CMD */
		ek80_send("\n");
		return SMF_EVENT_HANDLED;
	case EV_EK80_CMD:
		ek80_send("SLEEP\n");
		go(ST_WAIT_SHUTDOWN);
		return SMF_EVENT_HANDLED;
	default:
		return SMF_EVENT_PROPAGATE;
	}
}

static enum smf_state_result wait_shutdown_run(void *o)
{
	ARG_UNUSED(o);
	if (c.ev == EV_EK80_HANDSHAKE) {
		go(ST_POWER_OFF);
		return SMF_EVENT_HANDLED;
	}
	return SMF_EVENT_PROPAGATE;
}

static void power_off_entry(void *o)
{
	ARG_UNUSED(o);
	relay_coil_set(RELAY_COIL_EK80_OFF, true);
	set_deadline(CONFIG_EK80_RELAY_PULSE_MS);
}

static enum smf_state_result power_off_run(void *o)
{
	ARG_UNUSED(o);
	if (c.ev == EV_TIMEOUT) {
		port_puts(PORT_GLIDER, "CMD:\r\n");
		go(ST_OFF);
		return SMF_EVENT_HANDLED;
	}
	return SMF_EVENT_PROPAGATE;
}

static const struct smf_state states[ST_COUNT + 1] = {
	[ST_ROOT] = SMF_CREATE_STATE(NULL, root_run, NULL, NULL, NULL),
	[ST_RESET] = SMF_CREATE_STATE(reset_entry, reset_run, off_coil_exit, &states[ST_ROOT],
				      NULL),
	[ST_OFF] = SMF_CREATE_STATE(NULL, off_run, NULL, &states[ST_ROOT], NULL),
	[ST_POWER_ON] = SMF_CREATE_STATE(power_on_entry, power_on_run, power_on_exit,
					 &states[ST_ROOT], NULL),
	[ST_SEND_TIME] = SMF_CREATE_STATE(NULL, send_time_run, NULL, &states[ST_ROOT], NULL),
	[ST_SEND_SLEEP] = SMF_CREATE_STATE(NULL, send_sleep_run, NULL, &states[ST_ROOT], NULL),
	[ST_WAIT_SLEEP] = SMF_CREATE_STATE(NULL, wait_sleep_run, NULL, &states[ST_ROOT], NULL),
	[ST_WAKE_DELAY] = SMF_CREATE_STATE(wake_delay_entry, wake_delay_run, NULL,
					   &states[ST_ROOT], NULL),
	[ST_SEND_RUN] = SMF_CREATE_STATE(NULL, send_run_run, NULL, &states[ST_ROOT], NULL),
	[ST_RUNNING] = SMF_CREATE_STATE(NULL, running_run, NULL, &states[ST_ROOT], NULL),
	[ST_WAIT_SHUTDOWN] = SMF_CREATE_STATE(NULL, wait_shutdown_run, NULL, &states[ST_ROOT],
					      NULL),
	[ST_POWER_OFF] = SMF_CREATE_STATE(power_off_entry, power_off_run, off_coil_exit,
					  &states[ST_ROOT], NULL),
};

/* ------------------------------------------------------------------ */
/* API                                                                  */
/* ------------------------------------------------------------------ */

void controller_init(int64_t now_ms)
{
	memset(&c, 0, sizeof(c));
	c.now = now_ms;
	c.deadline = NO_DEADLINE;
	/* Make sure the EK80 is off after any reset */
	c.cur = ST_RESET;
	smf_set_initial(SMF_CTX(&c), &states[ST_RESET]);
}

void controller_poll(int64_t now_ms)
{
	if (now_ms >= c.deadline) {
		c.deadline = NO_DEADLINE;
		dispatch(EV_TIMEOUT, now_ms);
	}
}

int64_t controller_next_deadline(void)
{
	return c.deadline;
}

void controller_dive_start(const char *srtc_line, int64_t now_ms)
{
	strncpy(c.srtc, srtc_line, sizeof(c.srtc) - 1);
	c.srtc[sizeof(c.srtc) - 1] = '\0';
	dispatch(EV_DIVE_START, now_ms);
}

void controller_dive_stop(int64_t now_ms)
{
	dispatch(EV_DIVE_STOP, now_ms);
}

void controller_reset(int64_t now_ms)
{
	dispatch(EV_RESET, now_ms);
}

void controller_set_live(bool on)
{
	c.st.live = on;
}

void controller_set_end_voltage(int cv)
{
	c.st.end_cv = cv;
}

void controller_on_ek80_line(const char *line, int64_t now_ms)
{
	if (controller_ek80_rx_mode() != EK80_RX_PROCESS) {
		return;
	}
	LOG_DBG("<- EK80: %s", line);
	if (c.st.live) {
		port_puts(PORT_GLIDER, line);
		port_puts(PORT_GLIDER, "\r\n");
	}
	if (strncmp(line, "CMD", 3) == 0) {
		dispatch(EV_EK80_CMD, now_ms);
	} else if (strncmp(line, "HANDSHAKE", 9) == 0) {
		dispatch(EV_EK80_HANDSHAKE, now_ms);
	} else if (strlen(line) > 6 && strstr(line, "PING_OK") != NULL) {
		c.st.ping_ok = true;
	}
}

enum ek80_rx_mode controller_ek80_rx_mode(void)
{
	if (c.cur == ST_POWER_ON) {
		return EK80_RX_HOLD;
	}
	if (c.cur >= ST_SEND_TIME && c.cur <= ST_WAIT_SHUTDOWN) {
		return EK80_RX_PROCESS;
	}
	return EK80_RX_DISCARD;
}

uint32_t controller_ek80_session(void)
{
	return c.session;
}

enum ctrl_state controller_state(void)
{
	return c.cur;
}

const char *controller_state_name(enum ctrl_state st)
{
	static const char *const names[] = {
		[ST_RESET] = "RESET",
		[ST_OFF] = "OFF",
		[ST_POWER_ON] = "POWER_ON",
		[ST_SEND_TIME] = "SEND_TIME",
		[ST_SEND_SLEEP] = "SEND_SLEEP",
		[ST_WAIT_SLEEP] = "WAIT_SLEEP",
		[ST_WAKE_DELAY] = "WAKE_DELAY",
		[ST_SEND_RUN] = "SEND_RUN",
		[ST_RUNNING] = "RUNNING",
		[ST_WAIT_SHUTDOWN] = "WAIT_SHUTDOWN",
		[ST_POWER_OFF] = "POWER_OFF",
	};

	return (st < ARRAY_SIZE(names) && names[st]) ? names[st] : "?";
}

const struct ek80_status *controller_status(void)
{
	return &c.st;
}
