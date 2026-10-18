#include "glider/glider_cmd.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/logging/log.h>

#include "core/board_io.h"
#include "core/controller.h"
#include "core/port.h"

LOG_MODULE_DECLARE(ek80, CONFIG_EK80_LOG_LEVEL);

static bool starts_with(const char *s, const char *prefix)
{
	return strncmp(s, prefix, strlen(prefix)) == 0;
}

static void reply(const char *s)
{
	port_puts(PORT_GLIDER, s);
}

static void dive_cmd(const char *arg, int64_t now_ms)
{
	if (starts_with(arg, "SRTC")) {
		controller_dive_start(arg, now_ms);
	} else if (strcmp(arg, "STOP") == 0) {
		controller_set_end_voltage(vbat_read_cv());
		controller_dive_stop(now_ms);
	}
}

static void climb_cmd(const char *arg)
{
	/* The EK80 only runs on the dive; a climb start needs no action */
	if (strcmp(arg, "STOP") == 0) {
		reply("CMD:\r\n");
	}
}

void glider_on_line(char *line, int64_t now_ms)
{
	char out[48];

	LOG_INF("<- glider: %s", line);

	if (line[0] == '1') {
		dive_cmd(line + 1, now_ms);
	} else if (line[0] == '2') {
		climb_cmd(line + 1);
	} else if (starts_with(line, "VOLTAGE")) {
		const struct ek80_status *st = controller_status();

		snprintf(out, sizeof(out), "%d,%d,%d\r\nCMD:\r\n", st->ping_ok ? 1 : 0, st->start_cv,
			 st->end_cv);
		reply(out);
	} else if (starts_with(line, "live on")) {
		reply("live output on\r\n");
		controller_set_live(true);
	} else if (starts_with(line, "live off")) {
		reply("live output off\r\n");
		controller_set_live(false);
	} else if (line[0] == 'v') {
		snprintf(out, sizeof(out), "%d\r\n", vbat_read_cv());
		reply(out);
	} else if (line[0] == 'r') {
		controller_reset(now_ms);
	}
}
