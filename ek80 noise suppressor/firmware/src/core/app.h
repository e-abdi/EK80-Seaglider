/*
 * Hardware-independent application core: frames bytes from the two ports
 * into lines and routes them to the glider command parser and the EK80
 * controller. main.c feeds it from the UARTs; the unit tests feed it
 * scripted traffic.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "core/port.h"

void app_init(int64_t now_ms);
void app_feed(enum ek80_port port, const uint8_t *data, size_t len, int64_t now_ms);
/* Handle timeouts; call regularly and at least by app_next_deadline(). */
void app_poll(int64_t now_ms);
int64_t app_next_deadline(void);
