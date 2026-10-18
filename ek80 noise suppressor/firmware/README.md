# EK80 noise suppressor firmware (V3.0)

Zephyr firmware for the V3.0 board (Raspberry Pi Pico / RP2040). It handles the
glider protocol and the EK80 command sequence. The structure follows
[SMaRT](https://github.com/e-abdi/SMaRT): one event loop, interrupt-driven UARTs
with ring buffers, a Zephyr SMF state machine, a hardware watchdog, debug output
over SEGGER RTT, and host unit tests on `native_sim`.

## Build, flash, test

Needs Zephyr 4.3 (`west`) and a Zephyr SDK it accepts (0.17.4 or newer).
Zephyr's build cannot handle spaces in paths, and this repository lives under
`ek80 noise suppressor/`, so build through a link without spaces:

```bash
ln -s "$PWD/ek80 noise suppressor/firmware" ~/ek80-fw
cd ~/zephyrproject && source .venv/bin/activate

# Board image
west build -p -b rpi_pico ~/ek80-fw -d build-ek80
# Hold BOOTSEL, plug USB, copy build-ek80/zephyr/zephyr.uf2 to RPI-RP2 (or: west flash)

# Host tests
west build -p -b native_sim/native/64 ~/ek80-fw/tests/unit -d build-ek80-test
build-ek80-test/zephyr/zephyr.exe
```

The Pico runs from the board's `SENSOR3.3V` rail with its own regulator
disabled (`3V3_EN` tied low), so **USB alone does not power it**: connect the
pack (J5) when flashing over USB.

Debug output goes to SEGGER RTT on the Pico's SWD pins (the three pads at the
module end opposite USB). It never touches the glider or EK80 UART. The board
logs every state change, every command sent to the EK80, and a status line
once a minute.

### Bench test without a glider or an EK80

```bash
pip install pyserial
python3 tools/ek80_sim.py /dev/ttyUSB1               # RS-422 adapter on J3
python3 tools/glider_sim.py /dev/ttyUSB0 --dive 60 --live   # RS-232 adapter on J2
```

`ek80_sim.py` answers like an EK80 (CMD prompts, HANDSHAKE after SLEEP,
PING_OK lines while running). `glider_sim.py` plays one dive: `1SRTC`, wait,
`1STOP`, then `VOLTAGE`, `2SRTC`, `2STOP`.

## Glider protocol (38400 baud)

Lines end with CR and/or LF, are trimmed, and are matched by prefix in this
order. Replies end with CR LF.

| Glider sends | Board does | Reply |
|---|---|---|
| `1SRTC...` | dive start: relay to EK80, then run the sequence below; `SRTC...` is forwarded to the EK80 | none |
| `1STOP` | records the pack voltage; if the ensemble runs, sends a bare LF to stop it and powers the EK80 down; otherwise resets the relay to the charger | `CMD:` once the relay is back on the charger (no reply in the reset case) |
| `2SRTC...` | nothing | none |
| `2STOP` | nothing | `CMD:` |
| `VOLTAGE` | | `<ping_ok>,<start>,<end>` then `CMD:` (voltages in centivolts) |
| `live on` / `live off` | echo every EK80 line to the glider | `live output on` / `live output off` |
| `v...` | | pack voltage now, centivolts |
| `r...` | relay to charger, all state cleared, live output off | none |

## EK80 sequence (RS-422, 38400 baud)

```
                    1SRTC
 OFF ──────────────────────────► POWER_ON (on-coil pulse 500 ms, start voltage)
  ▲                                   │
  │                                   ▼
  │   CMD → "SRTC..."  CMD → "SLEEP"   HANDSHAKE   500 ms → "wake"   CMD → "RUN ENSEMBLE 1"
  │   SEND_TIME ─────► SEND_SLEEP ───► WAIT_SLEEP ─► WAKE_DELAY ───► SEND_RUN ─────► RUNNING
  │                                                                                    │
  │                                         1STOP → "\n" (EK80 answers CMD)            │
  │                                                                                    ▼
  └── "CMD:" to glider ◄── POWER_OFF (off-coil pulse 500 ms) ◄── HANDSHAKE ◄── WAIT_SHUTDOWN
                                                                  (CMD → "SLEEP")
```

Lines starting with `CMD` and `HANDSHAKE` advance the sequence; a line longer
than 6 characters containing `PING_OK` sets the ping flag reported by
`VOLTAGE`. `1STOP` outside `RUNNING`, `r`, and power-up all pulse the off coil
(`RESET`) without replying.

## Robustness

* **No blocking.** All timing (relay pulses, wake delay) is deadline-based, so
  the board keeps receiving on both ports at all times.
* **The relay coils are never driven together.** Both the state machine (exit
  actions) and the GPIO layer guarantee it, whatever the glider sends.
* **A second `1SRTC` restarts the sequence cleanly.**
* **The time line is stored when `1SRTC` arrives**, so later glider commands
  cannot change what the EK80 receives.
* **EK80 bytes are discarded while it is off**, and a partial line cannot carry
  over into the next power-up.
* **Watchdog (4 s).** A reset re-runs the power-up path, which switches the
  relay to the charger.
* **Line lengths**: up to 64 bytes from the glider and 256 from the EK80.
  Longer lines are dropped up to the next line end.
* **Pack voltage in centivolts**, from the R45/R46 divider ratio
  (`CONFIG_EK80_VBAT_DIVIDER_X1000`) with an optional calibration factor
  (`CONFIG_EK80_VBAT_CAL_PERMILLE`).

## Configuration

All options are in `Kconfig` with working defaults. Browse them with
`west build -t menuconfig`, or override them on the command line
(`-- -DCONFIG_EK80_WAKE_DELAY_MS=800`): baud rates, line lengths, relay pulse
and wake delay, the run command, the voltage divider and calibration, ADC
averaging, and the watchdog.

## Pin map (`boards/rpi_pico.overlay`)

| Pico | Function | Board net |
|---|---|---|
| GP16 / GP17 | UART0 TX / RX, EK80 via U1 (RS-422), J3 | STX / SRX |
| GP20 / GP21 | UART1 TX / RX, glider via U5/U2 optocouplers and U4 (RS-232), J2 | GTX / GRX |
| GP27 | pulse: pack → EK80 (EK80 on) | RELAY_RESET (Q2) |
| GP22 | pulse: pack → charger (EK80 off) | RELAY_SET (Q1) |
| GP26 / ADC0 | pack voltage through R45/R46 | VBAT_SENSE |
| GP28 | held low | BMS_PRES → JP2 → PRES (bq77915) |
| GP18 / GP19 | I2C1 to J6 (external BQ76920), not used yet | SDA / SCL |
| GP14 / GP15 | J6 ALM / BOOT, not used yet | ALM / BOOT |

Note the relay net names: a pulse on net `RELAY_RESET` powers the EK80 and one
on `RELAY_SET` switches it off. The firmware names the GPIOs by what they do.

## Source map

| Path | Content |
|---|---|
| `src/main.c` | event loop: UART wait, feed, watchdog |
| `src/core/app.c` | frames lines and routes them (shared with the tests) |
| `src/core/controller.c` | EK80 power and command state machine (Zephyr SMF) |
| `src/glider/glider_cmd.c` | glider command parser |
| `src/core/linebuf.c`, `src/hw/port_uart.c` | line framer and UART ports, from SMaRT (GPL-3.0) |
| `src/hw/board_io.c` | relay coils, pack voltage ADC, PRES line, watchdog |
| `tests/unit/` | ztest protocol scenarios on native_sim |
| `tools/` | bench simulators for the glider and the EK80 |
