#!/usr/bin/env python3
"""USB time sync, page selection, status, and framebuffer capture for Watch Shell."""

import argparse
import json
import time
from pathlib import Path

import serial
from serial.tools import list_ports


def connect(port=None):
    if not port:
        matches = [p.device for p in list_ports.comports() if p.vid == 0x303A and p.pid == 0x1001]
        if len(matches) != 1:
            raise SystemExit(
                f"Expected one ESP32 USB Serial/JTAG device, found {matches}; use --port"
            )
        port = matches[0]
    s = serial.Serial(port=None, baudrate=115200, timeout=1, write_timeout=3)
    # Assert DTR before opening so serial diagnostics do not enter the
    # USB-Serial/JTAG reset combination (DTR low with RTS high).
    s.dtr = True
    s.rts = False
    s.port = port
    s.open()
    return s


def transact(s, command, prefix, timeout=5):
    s.write((command + "\n").encode())
    s.flush()
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        line = s.readline().decode(errors="replace").strip()
        if line.startswith(prefix):
            return line
        if line.startswith("ERROR"):
            raise RuntimeError(line)
    raise TimeoutError(f"No {prefix!r} reply to {command!r}")


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--port")
    p.add_argument("--sync", action="store_true")
    p.add_argument("--ntp", action="store_true", help="Request time sync over Wi-Fi")
    p.add_argument(
        "--page",
        choices=[
            "face",
            "apps",
            "controls",
            "settings",
            "stopwatch",
            "timer",
            "sensors",
            "info",
            "wifi",
            "weather",
            "display",
        ],
    )
    p.add_argument("--wake", action="store_true")
    p.add_argument("--capture", type=Path)
    a = p.parse_args()
    with connect(a.port) as s:
        time.sleep(2)
        s.reset_input_buffer()
        if a.sync:
            print(transact(s, f"TIME {int(time.time())}", "OK TIME"))
        if a.ntp:
            print(transact(s, "NTP", "OK NTP"))
        if a.wake:
            print(transact(s, "WAKE", "OK WAKE"))
        if a.page:
            print(transact(s, f"PAGE {a.page}", "OK PAGE"))
        time.sleep(1)
        print(json.dumps(json.loads(transact(s, "STATUS", "{")), indent=2))
        if a.capture:
            from PIL import Image

            header = transact(s, "CAPTURE", "RGB565", timeout=5)
            _, w, h, n = header.split()
            n = int(n)
            data = bytearray()
            deadline = time.monotonic() + 30
            while len(data) < n and time.monotonic() < deadline:
                data.extend(s.read(min(8192, n - len(data))))
            if len(data) != n:
                raise RuntimeError(f"Incomplete framebuffer: {len(data)}/{n}")
            rgb = bytearray()
            for i in range(0, n, 2):
                v = data[i] | data[i + 1] << 8
                rgb.extend(
                    (
                        ((v >> 11) & 31) * 255 // 31,
                        ((v >> 5) & 63) * 255 // 63,
                        (v & 31) * 255 // 31,
                    )
                )
            a.capture.parent.mkdir(parents=True, exist_ok=True)
            Image.frombytes("RGB", (int(w), int(h)), bytes(rgb)).save(a.capture)
            print("Captured", a.capture)


if __name__ == "__main__":
    main()
