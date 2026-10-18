/*
 * End-to-end scenarios: glider commands and EK80 prompts in, relay pulses
 * and serial output checked against the glider/EK80 protocol.
 */
#include <string.h>
#include <zephyr/ztest.h>

#include "core/app.h"
#include "core/controller.h"
#include "fake_hw.h"

#define PULSE CONFIG_EK80_RELAY_PULSE_MS
#define WAKE  CONFIG_EK80_WAKE_DELAY_MS

static void before(void *f)
{
	ARG_UNUSED(f);
	fake_reset();
	app_init(0);
	/* Boot pulse on the off coil */
	advance_ms(PULSE);
	fake_tx_clear();
}

ZTEST_SUITE(ek80, NULL, NULL, before, NULL, NULL);

/* Drive a dive up to the running ensemble */
static void start_ensemble(void)
{
	glider_says("1SRTC 20261017120000\r\n");
	advance_ms(PULSE);
	ek80_says("CMD:\r\n");
	ek80_says("CMD:\r\n");
	ek80_says("HANDSHAKE\r\n");
	advance_ms(WAKE);
	ek80_says("CMD:\r\n");
	zassert_equal(controller_state(), ST_RUNNING);
}

ZTEST(ek80, test_boot_switches_relay_to_charger)
{
	const struct coil_event *ev;

	fake_reset();
	app_init(0);
	zassert_equal(controller_state(), ST_RESET);
	zassert_true(fake_coil_on(RELAY_COIL_EK80_OFF));
	advance_ms(PULSE);
	zassert_equal(controller_state(), ST_OFF);
	zassert_equal(fake_coil_events(&ev), 2);
	zassert_equal(ev[0].coil, RELAY_COIL_EK80_OFF);
	zassert_true(ev[0].on);
	zassert_equal(ev[1].t, PULSE);
	zassert_false(ev[1].on);
	zassert_equal(strlen(fake_tx(PORT_GLIDER)), 0, "boot is silent");
}

ZTEST(ek80, test_full_dive)
{
	const struct coil_event *ev;
	int n0 = fake_coil_events(&ev);

	fake_set_vbat_cv(1234);
	glider_says("1SRTC 20261017120000\r\n");
	zassert_equal(controller_state(), ST_POWER_ON);
	zassert_true(fake_coil_on(RELAY_COIL_EK80_ON));
	zassert_equal(strlen(fake_tx(PORT_GLIDER)), 0, "dive start gets no reply");

	advance_ms(PULSE - 10);
	zassert_true(fake_coil_on(RELAY_COIL_EK80_ON));
	advance_ms(10);
	zassert_false(fake_coil_on(RELAY_COIL_EK80_ON));
	zassert_equal(controller_state(), ST_SEND_TIME);
	zassert_equal(controller_status()->start_cv, 1234);

	ek80_says("CMD:\r\n");
	zassert_equal(strcmp(fake_tx(PORT_EK80), "SRTC 20261017120000\r\n"), 0);
	ek80_says("CMD:\r\n");
	zassert_equal(controller_state(), ST_WAIT_SLEEP);
	ek80_says("HANDSHAKE\r\n");
	advance_ms(WAKE - 10);
	zassert_equal(fake_count(PORT_EK80, "wake\n"), 0, "wake too early");
	advance_ms(10);
	zassert_equal(fake_count(PORT_EK80, "wake\n"), 1);
	ek80_says("CMD:\r\n");
	zassert_equal(controller_state(), ST_RUNNING);
	zassert_false(controller_status()->ping_ok);

	ek80_says("120001 PING_OK 1\r\n");
	zassert_true(controller_status()->ping_ok);

	fake_set_vbat_cv(1180);
	glider_says("1STOP\r\n");
	zassert_equal(controller_state(), ST_RUNNING, "waits for the EK80 to stop");
	ek80_says("CMD:\r\n");
	ek80_says("HANDSHAKE\r\n");
	zassert_equal(controller_state(), ST_POWER_OFF);
	zassert_true(fake_coil_on(RELAY_COIL_EK80_OFF));
	zassert_equal(strlen(fake_tx(PORT_GLIDER)), 0, "no prompt before the relay is off");

	advance_ms(PULSE);
	zassert_equal(controller_state(), ST_OFF);
	zassert_false(fake_coil_on(RELAY_COIL_EK80_OFF));
	zassert_equal(strcmp(fake_tx(PORT_GLIDER), "CMD:\r\n"), 0);
	zassert_equal(strcmp(fake_tx(PORT_EK80), "SRTC 20261017120000\r\n"
						 "SLEEP\n"
						 "wake\n"
						 "RUN ENSEMBLE 1\n"
						 "\n"
						 "SLEEP\n"),
		      0, "EK80 got: %s", fake_tx(PORT_EK80));

	fake_tx_clear();
	glider_says("VOLTAGE\r\n");
	zassert_equal(strcmp(fake_tx(PORT_GLIDER), "1,1234,1180\r\nCMD:\r\n"), 0);

	/* on, off, off-pulse on, off-pulse off */
	zassert_equal(fake_coil_events(&ev) - n0, 4);
	zassert_false(fake_coils_overlapped());
}

ZTEST(ek80, test_ensemble_ending_by_itself_powers_off)
{
	start_ensemble();
	ek80_says("CMD:\r\n");
	ek80_says("HANDSHAKE\r\n");
	advance_ms(PULSE);
	zassert_equal(controller_state(), ST_OFF);
	zassert_equal(strcmp(fake_tx(PORT_GLIDER), "CMD:\r\n"), 0);
}

ZTEST(ek80, test_dive_stop_before_running_resets_silently)
{
	glider_says("1SRTC 20261017120000\r\n");
	advance_ms(PULSE);
	zassert_equal(controller_state(), ST_SEND_TIME);

	fake_set_vbat_cv(1111);
	glider_says("1STOP\r\n");
	zassert_equal(controller_state(), ST_RESET);
	zassert_true(fake_coil_on(RELAY_COIL_EK80_OFF));
	advance_ms(PULSE);
	zassert_equal(controller_state(), ST_OFF);
	zassert_equal(strlen(fake_tx(PORT_GLIDER)), 0, "no prompt in the reset case");
	zassert_equal(controller_status()->end_cv, 1111);
}

ZTEST(ek80, test_dive_stop_during_power_off_drops_prompt)
{
	start_ensemble();
	ek80_says("CMD:\r\n");
	ek80_says("HANDSHAKE\r\n");
	zassert_equal(controller_state(), ST_POWER_OFF);
	glider_says("1STOP\r\n");
	advance_ms(PULSE);
	zassert_equal(controller_state(), ST_OFF);
	zassert_equal(strlen(fake_tx(PORT_GLIDER)), 0);
	zassert_false(fake_coils_overlapped());
}

ZTEST(ek80, test_climb_commands)
{
	glider_says("2SRTC 20261017130000\r\n");
	zassert_equal(controller_state(), ST_OFF);
	zassert_equal(strlen(fake_tx(PORT_GLIDER)), 0);
	glider_says("2STOP\r\n");
	zassert_equal(strcmp(fake_tx(PORT_GLIDER), "CMD:\r\n"), 0);
}

ZTEST(ek80, test_stop_must_match_exactly)
{
	glider_says("1STOPX\r\n");
	glider_says("2STOP NOW\r\n");
	zassert_equal(controller_state(), ST_OFF);
	zassert_equal(strlen(fake_tx(PORT_GLIDER)), 0);
}

ZTEST(ek80, test_live_output)
{
	glider_says("live on\r\n");
	zassert_equal(strcmp(fake_tx(PORT_GLIDER), "live output on\r\n"), 0);
	start_ensemble();
	fake_tx_clear();
	ek80_says("  depth 12.5 m  \r\n");
	zassert_equal(strcmp(fake_tx(PORT_GLIDER), "depth 12.5 m\r\n"), 0, "trimmed echo");

	fake_tx_clear();
	glider_says("live off\r\n");
	ek80_says("depth 13 m\r\n");
	zassert_equal(strcmp(fake_tx(PORT_GLIDER), "live output off\r\n"), 0);
}

ZTEST(ek80, test_reset_clears_live_and_ping)
{
	glider_says("live on\r\n");
	start_ensemble();
	ek80_says("xx PING_OK\r\n");
	zassert_true(controller_status()->ping_ok);
	glider_says("r\r\n");
	zassert_equal(controller_state(), ST_RESET);
	zassert_false(controller_status()->live);
	zassert_false(controller_status()->ping_ok);
	advance_ms(PULSE);
	zassert_equal(controller_state(), ST_OFF);
}

ZTEST(ek80, test_ping_ok_needs_a_long_line)
{
	start_ensemble();
	ek80_says("PING_O\r\n");
	ek80_says("PING_OK\r\n"); /* 7 chars: needs length > 6 */
	zassert_true(controller_status()->ping_ok);
}

ZTEST(ek80, test_voltage_query)
{
	fake_set_vbat_cv(1256);
	glider_says("v\r\n");
	zassert_equal(strcmp(fake_tx(PORT_GLIDER), "1256\r\n"), 0);
	fake_tx_clear();
	glider_says("VOLTAGE\r\n");
	zassert_equal(strcmp(fake_tx(PORT_GLIDER), "0,0,0\r\nCMD:\r\n"), 0);
}

ZTEST(ek80, test_prompts_wait_for_the_right_token)
{
	glider_says("1SRTC 1\r\n");
	advance_ms(PULSE);
	ek80_says("HANDSHAKE\r\n");
	zassert_equal(controller_state(), ST_SEND_TIME, "HANDSHAKE is not CMD");
	ek80_says("CMD:\r\n");
	ek80_says("CMD:\r\n");
	ek80_says("CMD:\r\n");
	zassert_equal(controller_state(), ST_WAIT_SLEEP, "extra CMD ignored");
	ek80_says("HANDSHAKE\r\n");
	ek80_says("CMD:\r\n");
	zassert_equal(controller_state(), ST_WAKE_DELAY, "CMD during the wake delay dropped");
	advance_ms(WAKE);
	zassert_equal(controller_state(), ST_SEND_RUN, "needs a fresh CMD");
}

ZTEST(ek80, test_ek80_ignored_when_off_or_powering_up)
{
	ek80_says("CMD:\r\n");
	zassert_equal(strlen(fake_tx(PORT_EK80)), 0);
	glider_says("1SRTC 1\r\n");
	ek80_says("CMD:\r\n");
	advance_ms(PULSE);
	zassert_equal(controller_state(), ST_SEND_TIME);
	zassert_equal(strlen(fake_tx(PORT_EK80)), 0);
}

ZTEST(ek80, test_partial_line_does_not_leak_between_sessions)
{
	start_ensemble();
	ek80_says("CMD:\r\nHANDSHAKE\r\nCM");
	advance_ms(PULSE);
	glider_says("1SRTC 2\r\n");
	advance_ms(PULSE);
	ek80_says("D:\r\n"); /* would read "CMD:" if the stale "CM" survived */
	zassert_equal(controller_state(), ST_SEND_TIME);
}

ZTEST(ek80, test_new_dive_start_restarts_sequence)
{
	start_ensemble();
	glider_says("1SRTC 20261017140000\r\n");
	zassert_equal(controller_state(), ST_POWER_ON);
	zassert_true(fake_coil_on(RELAY_COIL_EK80_ON));
	advance_ms(PULSE);
	fake_tx_clear();
	ek80_says("CMD:\r\n");
	zassert_equal(strcmp(fake_tx(PORT_EK80), "SRTC 20261017140000\r\n"), 0);
	zassert_false(fake_coils_overlapped());
}

ZTEST(ek80, test_commands_are_trimmed_and_cr_terminated)
{
	glider_says("   1SRTC 99  \r");
	advance_ms(PULSE);
	ek80_says("CMD:\n");
	zassert_equal(strcmp(fake_tx(PORT_EK80), "SRTC 99\r\n"), 0);
}

ZTEST(ek80, test_prefix_matching)
{
	fake_set_vbat_cv(1300);
	glider_says("version\r\n"); /* any line starting with 'v' */
	zassert_equal(strcmp(fake_tx(PORT_GLIDER), "1300\r\n"), 0);
	fake_tx_clear();
	glider_says("hello\r\n");
	zassert_equal(strlen(fake_tx(PORT_GLIDER)), 0);
	glider_says("restart\r\n"); /* any line starting with 'r' */
	zassert_equal(controller_state(), ST_RESET);
}
