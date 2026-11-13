#!/usr/bin/env python3
"""Fetch verified vendor display drivers and apply the clock's small integration changes."""

import argparse
import hashlib
import json
import urllib.request
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DRIVERS = ("Display_ST7701.cpp", "Display_ST7701.h", "TCA9554PWR.cpp", "TCA9554PWR.h")


def prepare(board, sketch):
    config = json.loads((ROOT / "boards" / board / "board.json").read_text())
    archive = ROOT / ".cache" / "waveshare-demo.zip"
    archive.parent.mkdir(exist_ok=True)
    if not archive.exists():
        partial = archive.with_suffix(".download")
        print("Downloading official Waveshare demo…", flush=True)
        urllib.request.urlretrieve(config["vendor_url"], partial)
        if hashlib.sha256(partial.read_bytes()).hexdigest() != config["vendor_sha256"]:
            partial.unlink()
            raise RuntimeError(
                "Vendor archive changed; review the new archive before updating its hash."
            )
        partial.replace(archive)
    if hashlib.sha256(archive.read_bytes()).hexdigest() != config["vendor_sha256"]:
        raise RuntimeError(f"Vendor archive checksum mismatch: {archive}")
    with zipfile.ZipFile(archive) as source:
        for name in DRIVERS:
            text = (
                source.read("Arduino/examples/LVGL_Arduino/" + name).decode().replace("\r\n", "\n")
            )
            if name.endswith(".h"):
                text = text.replace('#include "LVGL_Driver.h"', "#include <Arduino.h>")
                text = text.replace('#include "Touch_CST820.h"', "")
            if name == "Display_ST7701.cpp":
                text = text.replace("  Touch_Init();", "")
                for call in [
                    "esp_lcd_new_rgb_panel(&rgb_config, &panel_handle)",
                    "esp_lcd_panel_reset(panel_handle)",
                    "esp_lcd_panel_init(panel_handle)",
                ]:
                    assert text.count(call + ";") == 1, f"Unexpected driver source: {call}"
                    text = text.replace(call + ";", "ESP_ERROR_CHECK(" + call + ");")
            target = sketch / name
            if target.exists() and target.read_text() != text:
                raise RuntimeError(f"Refusing to replace locally modified driver: {target}")
            target.write_text(text)
    print(f"Prepared {len(DRIVERS)} board driver files.")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("sketch", type=Path)
    parser.add_argument("--board", default="waveshare-esp32-s3-touch-lcd-2.1")
    args = parser.parse_args()
    prepare(args.board, args.sketch.resolve())
