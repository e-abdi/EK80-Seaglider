#!/usr/bin/env python3
"""Bench glider simulator.

Plays the Seaglider side of one dive over a USB-serial (RS-232) adapter
wired to the glider connector J2:

  1SRTC <time>   dive start (no reply expected)
  live on        optional, echoes EK80 output while diving
  1STOP          dive stop, waits for CMD:
  VOLTAGE        prints ping_ok, start and end pack voltage (centivolts)
  2SRTC / 2STOP  climb, 2STOP answers CMD:

Example:
  python3 glider_sim.py /dev/ttyUSB0 --dive 60 --live
Requires pyserial (pip install pyserial).
"""
import argparse
import threading
import time

import serial


def reader(port, stop, prompt):
    buf = b""
    while not stop.is_set():
        buf += port.read(256)
        while b"\n" in buf:
            line, buf = buf.split(b"\n", 1)
            text = line.decode(errors="replace").strip()
            if text:
                print(f"  <- {text}", flush=True)
            if text == "CMD:":
                prompt.set()


def send(port, text):
    print(f"-> {text}", flush=True)
    port.write(text.encode() + b"\r\n")


def wait_prompt(prompt, timeout):
    ok = prompt.wait(timeout)
    if not ok:
        print(f"!! no CMD: within {timeout:.0f} s", flush=True)
    prompt.clear()
    return ok


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("port")
    ap.add_argument("--baud", type=int, default=38400)
    ap.add_argument("--dive", type=float, default=30.0, help="seconds between 1SRTC and 1STOP")
    ap.add_argument("--live", action="store_true", help="send 'live on' before the dive")
    ap.add_argument("--stop-timeout", type=float, default=60.0)
    args = ap.parse_args()

    port = serial.Serial(args.port, args.baud, timeout=0.1)
    stop, prompt = threading.Event(), threading.Event()
    threading.Thread(target=reader, args=(port, stop, prompt), daemon=True).start()

    try:
        if args.live:
            send(port, "live on")
            time.sleep(0.5)
        send(port, "1SRTC " + time.strftime("%Y%m%d%H%M%S", time.gmtime()))
        time.sleep(args.dive)
        send(port, "1STOP")
        wait_prompt(prompt, args.stop_timeout)
        send(port, "VOLTAGE")
        wait_prompt(prompt, 5)
        send(port, "2SRTC " + time.strftime("%Y%m%d%H%M%S", time.gmtime()))
        time.sleep(1)
        send(port, "2STOP")
        wait_prompt(prompt, 5)
        if args.live:
            send(port, "live off")
            time.sleep(0.5)
    finally:
        stop.set()


if __name__ == "__main__":
    main()
