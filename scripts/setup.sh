#!/bin/sh
# Bootstrap the development environment on macOS or Linux.
set -eu
PROJECT_ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$PROJECT_ROOT"
python3 -c 'import sys; assert sys.version_info >= (3, 11), "Python 3.11+ required"'
mkdir -p .tools .cache
if [ ! -x .venv/bin/python ]; then python3 -m venv .venv; fi
.venv/bin/python -m pip install -r requirements-dev.txt

if [ ! -x .tools/arduino-cli ]; then
  case "$(uname -s)" in Darwin) TASK_OS=macOS ;; Linux) TASK_OS=Linux ;; *) echo 'Supported hosts: macOS / Linux' >&2; exit 1 ;; esac
  case "$(uname -m)" in arm64|aarch64) TASK_ARCH=ARM64 ;; x86_64) TASK_ARCH=64bit ;; *) echo 'Supported architectures: ARM64 / x86_64' >&2; exit 1 ;; esac
  TASK_ARCHIVE="arduino-cli_1.6.0_${TASK_OS}_${TASK_ARCH}.tar.gz"
  curl -fL --retry 3 "https://github.com/arduino/arduino-cli/releases/download/v1.6.0/$TASK_ARCHIVE" -o ".cache/$TASK_ARCHIVE"
  curl -fL --retry 3 'https://github.com/arduino/arduino-cli/releases/download/v1.6.0/1.6.0-checksums.txt' -o .cache/arduino-checksums.txt
  .venv/bin/python - "$TASK_ARCHIVE" <<'PY'
import hashlib
from pathlib import Path
import sys
name = sys.argv[1]
entries = [line.split() for line in Path('.cache/arduino-checksums.txt').read_text().splitlines() if line.strip()]
expected = next(parts[0] for parts in entries if parts[-1].lstrip('*') == name)
actual = hashlib.sha256((Path('.cache') / name).read_bytes()).hexdigest()
if actual != expected:
    raise SystemExit('Arduino CLI archive checksum mismatch')
PY
  tar -xzf ".cache/$TASK_ARCHIVE" -C .tools arduino-cli
fi
.tools/arduino-cli core update-index --additional-urls https://espressif.github.io/arduino-esp32/package_esp32_index.json
.tools/arduino-cli core install esp32:esp32@3.0.7 --additional-urls https://espressif.github.io/arduino-esp32/package_esp32_index.json

# Arduino's packaged ctags is Intel-only on macOS. Build the same upstream tool natively.
if [ "$(uname -s)" = Darwin ] && [ "$(uname -m)" = arm64 ] && [ ! -x .tools/ctags-source/ctags ]; then
  if [ ! -d .tools/ctags-source/.git ]; then
    git clone https://github.com/arduino/ctags.git .tools/ctags-source
  fi
  (
    cd .tools/ctags-source
    git checkout --detach 4464d1685148c1f8a1d0f7cd9343345e10bcc6c1
    ./configure
    make -j4
  )
fi
echo 'Ready. Build: .venv/bin/python scripts/build.py gravity-clock'
