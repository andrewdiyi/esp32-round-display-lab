# Third-party sources

## Barlow fonts

- Authors: The Barlow Project Authors (https://github.com/jpt/barlow).
- Source: Google Fonts, `ofl/barlow` and `ofl/barlowsemicondensed`.
- Included assets: `Barlow-Regular.ttf`, `BarlowSemiCondensed-Medium.ttf` and their complete `OFL.txt` files.
- License: SIL Open Font License 1.1. Generated bitmap coverage in `ClockFonts.h` is derived from these fonts and is covered by the font license.

The original personal-device prototype used locally installed proprietary fonts. Neither those font files nor their generated glyph tables are included in this repository.

## Waveshare board drivers

Downloaded locally from the official [ESP32-S3-Touch-LCD-2.1 demo archive](https://files.waveshare.com/wiki/ESP32-S3-Touch-LCD-2.1/ESP32-S3-Touch-LCD-2.1-Demo.zip). No project-level license was found in that archive. The repository includes a download/verification/integration script instead of redistributing the four vendor driver files. The download remains subject to its upstream terms.

## Development tools

Arduino CLI, Arduino-ESP32, Espressif's tools, Python packages and Arduino ctags are installed locally rather than vendored. Their upstream licenses continue to apply. On Apple Silicon, the setup script builds [arduino/ctags](https://github.com/arduino/ctags) at commit `4464d1685148c1f8a1d0f7cd9343345e10bcc6c1`; its source includes `COPYING`.

No blanket project license has been assigned to the original application and tooling code in this repository.
