#include "fake_hw.h"

#include <string.h>
#include <zephyr/sys/util.h>

#include "core/app.h"

#define TX_CAP     8192
#define MAX_EVENTS 64

static char tx[PORT_NUM][TX_CAP];
static size_t tx_len[PORT_NUM];
static struct port_stats stats[PORT_NUM];
static struct coil_event events[MAX_EVENTS];
static int n_events;
static bool coil_on[2];
static bool overlapped;
static int vbat_cv;
int64_t t_now;

void fake_reset(void)
{
	fake_tx_clear();
	memset(stats, 0, sizeof(stats));
	n_events = 0;
	coil_on[0] = coil_on[1] = false;
	overlapped = false;
	vbat_cv = 1200;
	t_now = 0;
}

int port_init(void)
{
	return 0;
}

void port_wait_ms(int32_t ms)
{
	ARG_UNUSED(ms);
}

void port_write(enum ek80_port port, const char *data, size_t len)
{
	size_t room = TX_CAP - 1 - tx_len[port];
	size_t n = len < room ? len : room;

	memcpy(&tx[port][tx_len[port]], data, n);
	tx_len[port] += n;
	tx[port][tx_len[port]] = '\0';
}

size_t port_read(enum ek80_port port, uint8_t *buf, size_t cap)
{
	ARG_UNUSED(port);
	ARG_UNUSED(buf);
	ARG_UNUSED(cap);
	return 0;
}

void port_discard_rx(enum ek80_port port)
{
	ARG_UNUSED(port);
}

const struct port_stats *port_get_stats(enum ek80_port port)
{
	return &stats[port];
}

int board_io_init(void)
{
	return 0;
}

void relay_coil_set(enum relay_coil coil, bool energized)
{
	if (energized && coil_on[!coil]) {
		overlapped = true;
	}
	coil_on[coil] = energized;
	if (n_events < MAX_EVENTS) {
		events[n_events++] = (struct coil_event){.coil = coil, .on = energized, .t = t_now};
	}
}

int vbat_read_cv(void)
{
	return vbat_cv;
}

void board_watchdog_feed(void)
{
}

const char *fake_tx(enum ek80_port port)
{
	return tx[port];
}

void fake_tx_clear(void)
{
	memset(tx_len, 0, sizeof(tx_len));
	tx[PORT_GLIDER][0] = tx[PORT_EK80][0] = '\0';
}

int fake_count(enum ek80_port port, const char *needle)
{
	int n = 0;
	size_t len = strlen(needle);

	for (const char *p = tx[port]; (p = strstr(p, needle)) != NULL; p += len) {
		n++;
	}
	return n;
}

void fake_set_vbat_cv(int cv)
{
	vbat_cv = cv;
}

int fake_coil_events(const struct coil_event **ev)
{
	*ev = events;
	return n_events;
}

bool fake_coil_on(enum relay_coil coil)
{
	return coil_on[coil];
}

bool fake_coils_overlapped(void)
{
	return overlapped;
}

void glider_says(const char *text)
{
	app_feed(PORT_GLIDER, (const uint8_t *)text, strlen(text), t_now);
}

void ek80_says(const char *text)
{
	app_feed(PORT_EK80, (const uint8_t *)text, strlen(text), t_now);
}

void advance_ms(int ms)
{
	for (int i = 0; i < ms; i += 10) {
		t_now += 10;
		app_poll(t_now);
	}
}
