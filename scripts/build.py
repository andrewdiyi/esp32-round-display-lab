#!/usr/bin/env python3
"""Compile a named example using its board definition; never flash a device."""

import argparse
import json
import os
import shutil
import subprocess
from pathlib import Path

from prepare_board import ROOT, prepare


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("example", nargs="?", default="gravity-clock")
    args = parser.parse_args()
    example = ROOT / "examples" / args.example
    config = json.loads((example / "example.json").read_text())
    board = json.loads((ROOT / "boards" / config["board"] / "board.json").read_text())
    sketch = example / config["sketch"]
    prepare(config["board"], sketch)
    cli = os.environ.get("ARDUINO_CLI") or str(ROOT / ".tools" / "arduino-cli")
    if not Path(cli).is_file():
        cli = shutil.which(cli) or shutil.which("arduino-cli")
    if not cli:
        raise SystemExit("Arduino CLI missing. Run: sh scripts/setup.sh")
    output = ROOT / "build" / args.example
    properties = ["--build-property", f"tools.esptool_py.path={ROOT / '.venv' / 'bin'}"]
    native_ctags = ROOT / ".tools" / "ctags-source"
    if (native_ctags / "ctags").exists():
        properties += ["--build-property", f"runtime.tools.ctags.path={native_ctags}"]
    command = [cli, "compile", "--fqbn", board["fqbn"], *properties]
    subprocess.run(
        command
        + [
            "--build-path",
            str(output / "arduino"),
            "--output-dir",
            str(output / "firmware"),
            str(sketch),
        ],
        check=True,
    )
    expanded = subprocess.check_output(
        command + ["--show-properties=expanded", str(sketch)], text=True
    )
    values = dict(line.split("=", 1) for line in expanded.splitlines() if "=" in line)
    boot_app = Path(values["runtime.platform.path"]) / "tools" / "partitions" / "boot_app0.bin"
    shutil.copy2(boot_app, output / "firmware" / "boot_app0.bin")
    (output / "board.json").write_text(json.dumps(board, indent=2) + "\n")
    print(f"Build complete: {output / 'firmware'}")


if __name__ == "__main__":
    main()
