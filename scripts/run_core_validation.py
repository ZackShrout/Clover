#!/usr/bin/env python3

from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path


DEFAULT_BSNES_CORE = Path("/Users/zshrout/dev/bsnes/bsnes/out/bsnes_libretro.dylib")
DEFAULT_STEP_LIMIT = 10_000_000
DEFAULT_SWEEP_STEP_LIMIT = 50_000_000
DEFAULT_FRAME_TARGET = 300

DEFAULT_ROMS: dict[str, dict[str, object]] = {
    "smw": {
        "path": "roms/local/Super Mario World (USA).sfc",
        "frames": "80,83,86,90",
    },
    "zelda": {
        "path": "roms/local/Legend of Zelda, The - A Link to the Past (USA).sfc",
        "frames": "80,83,86,90",
    },
    "ff3": {
        "path": "roms/local/Final Fantasy 3 (USA).smc",
        "frames": "117,130,300",
    },
}


def run(command: list[str], cwd: Path) -> subprocess.CompletedProcess[str]:
    print("$", " ".join(command), flush=True)
    return subprocess.run(command, cwd=cwd, text=True, capture_output=True)


def emit(result: subprocess.CompletedProcess[str]) -> None:
    sys.stdout.write(result.stdout)
    sys.stderr.write(result.stderr)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Run the standardized low-noise Clover core validation loop."
    )
    parser.add_argument(
        "--build-dir",
        default="build",
        help="Build directory containing Clover test binaries"
    )
    parser.add_argument(
        "--bsnes-core",
        default=str(DEFAULT_BSNES_CORE),
        help="Path to the bsnes libretro core"
    )
    parser.add_argument(
        "--rom-set",
        default="smw,zelda,ff3",
        help="Comma-separated ROM keys to validate: smw, zelda, ff3"
    )
    parser.add_argument(
        "--frame-target",
        type=int,
        default=DEFAULT_FRAME_TARGET,
        help="Frame target for the local regression pass"
    )
    parser.add_argument(
        "--step-limit",
        type=int,
        default=DEFAULT_STEP_LIMIT,
        help="Step limit for the local regression pass"
    )
    parser.add_argument(
        "--sweep-step-limit",
        type=int,
        default=DEFAULT_SWEEP_STEP_LIMIT,
        help="Step limit for the frame reference sweeps"
    )
    args = parser.parse_args()

    workspace = Path(__file__).resolve().parent.parent
    build_dir = (workspace / args.build_dir).resolve()
    bsnes_core = Path(args.bsnes_core)
    rom_keys = [key.strip().lower() for key in args.rom_set.split(",") if key.strip()]

    required_tools = [
        build_dir / "clover_hardware_loop_test",
        build_dir / "clover_local_rom_regression_test",
        workspace / "scripts" / "run_reference_sweep.py",
    ]
    for tool in required_tools:
        if not tool.exists():
            print(f"Missing required tool: {tool}", file=sys.stderr)
            return 1

    if not bsnes_core.exists():
        print(f"bsnes core not found: {bsnes_core}", file=sys.stderr)
        return 1

    invalid_keys = [key for key in rom_keys if key not in DEFAULT_ROMS]
    if invalid_keys:
        print(f"Unknown ROM keys: {', '.join(invalid_keys)}", file=sys.stderr)
        return 1

    failures: list[str] = []

    hardware_result = run([str(build_dir / "clover_hardware_loop_test")], workspace)
    emit(hardware_result)
    if hardware_result.returncode != 0:
        failures.append("hardware loop test")

    for rom_key in rom_keys:
        config = DEFAULT_ROMS[rom_key]
        rom_path = str(config["path"])
        frames = str(config["frames"])

        regression_result = run(
            [
                str(build_dir / "clover_local_rom_regression_test"),
                rom_path,
                str(args.frame_target),
                str(args.step_limit),
            ],
            workspace,
        )
        emit(regression_result)
        if regression_result.returncode != 0:
            failures.append(f"{rom_key} local regression")

        sweep_result = run(
            [
                sys.executable,
                str(workspace / "scripts" / "run_reference_sweep.py"),
                rom_path,
                "--frames",
                frames,
                "--step-limit",
                str(args.sweep_step_limit),
                "--output-dir",
                f"reference-sweep-{rom_key}",
            ],
            workspace,
        )
        emit(sweep_result)
        if sweep_result.returncode != 0:
            failures.append(f"{rom_key} reference sweep")

    if failures:
        print("Core validation failed:")
        for failure in failures:
            print(f"  {failure}")
        return 1

    print("Core validation passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
