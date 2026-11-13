#!/usr/bin/env python3
"""Flash a built example to an explicitly selected serial port."""

import argparse
import json
import subprocess
import sys
from pathlib import Path

from serial.tools import list_ports

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("example", nargs="?", default="gravity-clock")
    parser.add_argument("--port", required=True)
    parser.add_argument("--expected-serial", help="Optional additional USB serial-number match")
    args = parser.parse_args()
    config = json.loads((ROOT / "examples" / args.example / "example.json").read_text())
    output = ROOT / "build" / args.example
    board = json.loads((output / "board.json").read_text())
    ports = [
        p
        for p in list_ports.comports()
        if p.device == args.port and p.vid == board["usb_vid"] and p.pid == board["usb_pid"]
    ]
    if len(ports) != 1:
        raise SystemExit("Selected port is not an Espressif USB Serial/JTAG device.")
    if (
        args.expected_serial
        and (ports[0].serial_number or "").casefold() != args.expected_serial.casefold()
    ):
        raise SystemExit("Selected port does not match --expected-serial.")
    stem = config["sketch"] + ".ino"
    firmware = output / "firmware"
    images = [
        ("0x0", firmware / (stem + ".bootloader.bin")),
        ("0x8000", firmware / (stem + ".partitions.bin")),
        ("0xe000", firmware / "boot_app0.bin"),
        ("0x10000", firmware / (stem + ".bin")),
    ]
    for _, path in images:
        if not path.is_file():
            raise SystemExit(f"Missing image: {path}; build the example first.")
    command = [
        sys.executable,
        "-m",
        "esptool",
        "--chip",
        board["chip"],
        "--port",
        args.port,
        "--baud",
        "460800",
        "write-flash",
        "--flash-mode",
        board["flash_mode"],
        "--flash-freq",
        board["flash_freq"],
        "--flash-size",
        board["flash_size"],
    ]
    for address, path in images:
        command += [address, str(path)]
    subprocess.run(command, check=True)
    print("Flash verified. Run the example's USB control tool with --sync to initialize the RTC.")


if __name__ == "__main__":
    main()
