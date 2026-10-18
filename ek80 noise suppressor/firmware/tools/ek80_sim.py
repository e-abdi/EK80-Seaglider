#!/usr/bin/env python3
"""Bench EK80 emulator.

Plays the EK80 side of the command sequence over a USB-RS422 adapter wired
to J3, so the board can be tested without the echosounder.

The emulator cannot see the relay, so until it is told the time it prints
the CMD prompt every few seconds, as a freshly powered EK80 would. Prompts
that arrive while the board is not listening are ignored by the board.

  SRTC ...         -> CMD:
  SLEEP            -> HANDSHAKE (and back to prompting if it was running)
  wake             -> CMD:
  RUN ENSEMBLE 1   -> PING_OK lines every second
  <empty line>     -> ensemble interrupted -> CMD:

Example:
  python3 ek80_sim.py /dev/ttyUSB1
Requires pyserial (pip install pyserial).
"""
import argparse
import time

import serial


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("port")
    ap.add_argument("--baud", type=int, default=38400)
    ap.add_argument("--prompt-every", type=float, default=3.0,
                    help="seconds between CMD prompts while waiting for SRTC")
    args = ap.parse_args()

    port = serial.Serial(args.port, args.baud, timeout=0.1)
    state = "boot"
    line = b""
    last_cr = False
    next_prompt = time.monotonic() + args.prompt_every
    next_ping = 0.0
    pings = 0

    def say(text):
        print(f"EK80 -> {text}", flush=True)
        port.write(text.encode() + b"\r\n")

    while True:
        now = time.monotonic()
        if state == "boot" and now >= next_prompt:
            say("CMD:")
            next_prompt = now + args.prompt_every
        if state == "running" and now >= next_ping:
            pings += 1
            say(f"{time.strftime('%H%M%S')} PING_OK {pings}")
            next_ping = now + 1.0

        for b in port.read(256):
            c = bytes([b])
            crlf = c == b"\n" and last_cr
            last_cr = c == b"\r"
            if c not in (b"\r", b"\n"):
                line += c
                continue
            if crlf:
                continue
            text = line.decode(errors="replace").strip()
            line = b""
            print(f"EK80 <- {text or '<LF>'}  [{state}]", flush=True)
            if text.startswith("SRTC"):
                state = "set"
                say("CMD:")
            elif text == "SLEEP":
                state = "boot" if state == "running_stopped" else "asleep"
                say("HANDSHAKE")
                next_prompt = time.monotonic() + args.prompt_every
            elif text == "wake":
                state = "awake"
                say("CMD:")
            elif text.startswith("RUN ENSEMBLE"):
                state = "running"
                pings = 0
                next_ping = time.monotonic() + 1.0
            elif text == "" and state == "running":
                state = "running_stopped"
                say("CMD:")


if __name__ == "__main__":
    main()
