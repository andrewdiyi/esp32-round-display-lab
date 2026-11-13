#!/bin/sh
set -eu
PROJECT_ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$PROJECT_ROOT"
mkdir -p build/tests
"${CXX:-c++}" -std=c++17 -O2 examples/gravity-clock/tests/test_clock.cpp -o build/tests/test-clock
build/tests/test-clock build/tests/preview.ppm
.venv/bin/python -m compileall -q scripts examples/gravity-clock/clock_control.py
.venv/bin/python scripts/flash.py --help >/dev/null
.venv/bin/python examples/gravity-clock/clock_control.py --help >/dev/null
.venv/bin/python - <<'PY'
from PIL import Image
Image.open('build/tests/preview.ppm').save('build/tests/preview.png')
PY
