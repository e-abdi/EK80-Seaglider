#include "core/app.h"

#include <ctype.h>
#include <string.h>

#include <zephyr/logging/log.h>

#include "core/controller.h"
#include "core/linebuf.h"
#include "glider/glider_cmd.h"

LOG_MODULE_REGISTER(ek80, CONFIG_EK80_LOG_LEVEL);

static char glider_mem[CONFIG_EK80_GLIDER_LINE_MAX];
static char ek80_mem[CONFIG_EK80_SENSOR_LINE_MAX];
static struct linebuf glider_lb;
static struct linebuf ek80_lb;
static uint32_t ek80_session;

/* Strip leading and trailing whitespace in place, like SafeString::trim() */
static char *trim(char *s)
{
	size_t n;

	while (*s != '\0' && isspace((unsigned char)*s)) {
		s++;
	}
	n = strlen(s);
	while (n > 0 && isspace((unsigned char)s[n - 1])) {
		s[--n] = '\0';
	}
	return s;
}

void app_init(int64_t now_ms)
{
	linebuf_init(&glider_lb, glider_mem, sizeof(glider_mem));
	linebuf_init(&ek80_lb, ek80_mem, sizeof(ek80_mem));
	ek80_session = 0;
	controller_init(now_ms);
}

void app_feed(enum ek80_port port, const uint8_t *data, size_t len, int64_t now_ms)
{
	struct linebuf *lb = (port == PORT_GLIDER) ? &glider_lb : &ek80_lb;

	if (port == PORT_EK80) {
		if (controller_ek80_rx_mode() != EK80_RX_PROCESS) {
			return;
		}
		/* No partial line may leak from one EK80 session into the next */
		if (ek80_session != controller_ek80_session()) {
			ek80_session = controller_ek80_session();
			linebuf_reset(lb);
		}
	}
	for (size_t i = 0; i < len; i++) {
		if (!linebuf_feed(lb, (char)data[i], now_ms)) {
			continue;
		}
		char *line = trim(lb->buf);

		if (*line == '\0') {
			continue;
		}
		if (port == PORT_GLIDER) {
			glider_on_line(line, now_ms);
		} else {
			controller_on_ek80_line(line, now_ms);
		}
	}
}

void app_poll(int64_t now_ms)
{
	controller_poll(now_ms);
}

int64_t app_next_deadline(void)
{
	return controller_next_deadline();
}
