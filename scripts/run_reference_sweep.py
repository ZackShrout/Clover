#!/usr/bin/env python3

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path


DEFAULT_FRAMES = [120, 140, 150, 240]
DEFAULT_ROM = Path("roms/local/Super Mario World (USA).sfc")
DEFAULT_BSNES_CORE = Path("/Users/zshrout/dev/bsnes/bsnes/out/bsnes_libretro.dylib")


def run(command: list[str], cwd: Path, env: dict[str, str] | None = None) -> subprocess.CompletedProcess[str]:
    print("$", " ".join(command), flush=True)
    return subprocess.run(command, cwd=cwd, text=True, capture_output=True, env=env)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Capture Clover and bsnes reference frames at a set of checkpoints and compare them."
    )
    parser.add_argument("rom", nargs="?", default=str(DEFAULT_ROM), help="ROM path to sweep")
    parser.add_argument(
        "--frames",
        default=",".join(str(frame) for frame in DEFAULT_FRAMES),
        help="Comma-separated checkpoint frame numbers"
    )
    parser.add_argument(
        "--step-limit",
        type=int,
        default=50_000_000,
        help="Hardware step limit passed to clover_rom_bringup"
    )
    parser.add_argument(
        "--build-dir",
        default="build",
        help="Build directory containing clover_rom_bringup, clover_bsnes_bringup, and clover_frame_compare"
    )
    parser.add_argument(
        "--output-dir",
        default="reference-sweep",
        help="Directory where captured frames are written"
    )
    parser.add_argument(
        "--bsnes-core",
        default=str(DEFAULT_BSNES_CORE),
        help="Path to the bsnes libretro core"
    )
    parser.add_argument(
        "--keep-existing",
        action="store_true",
        help="Do not clear the output directory before running"
    )
    parser.add_argument(
        "--verbose-bringup",
        action="store_true",
        help="Allow Clover bringup to emit full debug diagnostics instead of the standard low-noise summary"
    )
    args = parser.parse_args()

    workspace = Path(__file__).resolve().parent.parent
    rom_path = (workspace / args.rom).resolve() if not Path(args.rom).is_absolute() else Path(args.rom)
    build_dir = (workspace / args.build_dir).resolve()
    output_dir = (workspace / args.output_dir).resolve()
    bsnes_core = Path(args.bsnes_core)

    frames = [int(frame) for frame in args.frames.split(",") if frame.strip()]
    if not frames:
        print("No frame checkpoints provided.", file=sys.stderr)
        return 1

    required_tools = {
        "clover_rom_bringup": build_dir / "clover_rom_bringup",
        "clover_bsnes_bringup": build_dir / "clover_bsnes_bringup",
        "clover_frame_compare": build_dir / "clover_frame_compare",
    }
    for label, tool in required_tools.items():
        if not tool.exists():
            print(f"Missing tool: {label} at {tool}", file=sys.stderr)
            return 1

    if not rom_path.exists():
        print(f"ROM not found: {rom_path}", file=sys.stderr)
        return 1

    if not bsnes_core.exists():
        print(f"bsnes core not found: {bsnes_core}", file=sys.stderr)
        return 1

    if output_dir.exists() and not args.keep_existing:
        shutil.rmtree(output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)

    failures: list[tuple[int, str]] = []
    clover_env = os.environ.copy()
    if not args.verbose_bringup:
        clover_env["CLOVER_BRINGUP_VERBOSE"] = "0"
    for frame in frames:
        bsnes_dump_dir = output_dir / f"bsnes-frame-{frame:04d}"
        clover_dump_dir = output_dir / f"clover-frame-{frame:04d}"

        bsnes_result = run(
            [
                str(required_tools["clover_bsnes_bringup"]),
                str(rom_path),
                str(frame),
                str(bsnes_dump_dir),
                "1",
                str(frame),
                str(bsnes_core),
            ],
            workspace,
        )
        sys.stdout.write(bsnes_result.stdout)
        sys.stderr.write(bsnes_result.stderr)
        if bsnes_result.returncode != 0:
            failures.append((frame, "bsnes capture failed"))
            continue

        clover_result = run(
            [
                str(required_tools["clover_rom_bringup"]),
                str(rom_path),
                str(frame),
                str(args.step_limit),
                str(clover_dump_dir),
                "1",
                str(frame),
            ],
            workspace,
            env=clover_env,
        )
        sys.stdout.write(clover_result.stdout)
        sys.stderr.write(clover_result.stderr)
        if clover_result.returncode != 0:
            failures.append((frame, "clover capture failed"))
            continue

        compare_result = run(
            [
                str(required_tools["clover_frame_compare"]),
                str(bsnes_dump_dir / f"frame_{frame}.ppm"),
                str(clover_dump_dir / f"frame_{frame}.ppm"),
            ],
            workspace,
        )
        sys.stdout.write(compare_result.stdout)
        sys.stderr.write(compare_result.stderr)
        if compare_result.returncode != 0:
            failures.append((frame, "frame mismatch"))

    if failures:
        print("Reference sweep failed:")
        for frame, reason in failures:
            print(f"  frame {frame}: {reason}")
        return 1

    print("Reference sweep passed for frames:", ", ".join(str(frame) for frame in frames))
    print(f"Artifacts: {output_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
