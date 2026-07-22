#!/usr/bin/env python3

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path
from typing import Any


DEFAULT_MANIFEST = Path("validation/accuracy_fence.json")
DEFAULT_BSNES_CORE = Path("../bsnes/bsnes/out/bsnes_libretro.dylib")
REQUIRED_TOOLS = (
    "clover_rom_bringup",
    "clover_bsnes_bringup",
    "clover_frame_range_compare",
)


@dataclass(frozen=True)
class scenario_t:
    id: str
    suite: str
    rom: Path
    sha256: str
    input_script: Path | None
    save_ram: Path | None
    save_ram_sha256: str | None
    bsnes_input_frame_offset: int
    start_frame: int
    end_frame: int
    step_limit: int
    compare_profile: str


def load_manifest(path: Path, workspace: Path) -> list[scenario_t]:
    try:
        raw: dict[str, Any] = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise ValueError(f"unable to read manifest {path}: {error}") from error

    if raw.get("version") != 1:
        raise ValueError("accuracy-fence manifest version must be 1")

    defaults = raw.get("defaults", {})
    raw_scenarios = raw.get("scenarios")
    if not isinstance(raw_scenarios, list) or not raw_scenarios:
        raise ValueError("accuracy-fence manifest must contain scenarios")

    scenarios: list[scenario_t] = []
    seen_ids: set[str] = set()
    for index, item in enumerate(raw_scenarios):
        if not isinstance(item, dict):
            raise ValueError(f"scenario {index} must be an object")
        try:
            scenario_id = str(item["id"])
            suite = str(item["suite"])
            rom = workspace / str(item["rom"])
            sha256 = str(item["sha256"]).lower()
            start_frame = int(item["start_frame"])
            end_frame = int(item["end_frame"])
            step_limit = int(item.get("step_limit", defaults["step_limit"]))
            compare_profile = str(item.get("compare_profile", defaults["compare_profile"]))
            bsnes_input_frame_offset = int(item.get("bsnes_input_frame_offset", 0))
        except (KeyError, TypeError, ValueError) as error:
            raise ValueError(f"scenario {index} has an invalid or missing field: {error}") from error

        if not scenario_id or scenario_id in seen_ids:
            raise ValueError(f"scenario id is empty or duplicated: {scenario_id!r}")
        if suite not in {"baseline", "interactive", "investigation"}:
            raise ValueError(f"scenario {scenario_id} has unknown suite {suite!r}")
        if start_frame < 1 or end_frame < start_frame:
            raise ValueError(f"scenario {scenario_id} has invalid frame range")
        if step_limit < 1:
            raise ValueError(f"scenario {scenario_id} has invalid step limit")
        if compare_profile != "exact":
            raise ValueError(f"scenario {scenario_id} must use the exact compare profile")
        if bsnes_input_frame_offset < -120 or bsnes_input_frame_offset > 120:
            raise ValueError(f"scenario {scenario_id} has an unreasonable input frame offset")
        if not re.fullmatch(r"[0-9a-f]{64}", sha256):
            raise ValueError(f"scenario {scenario_id} has invalid SHA-256")

        input_script_raw = item.get("input_script")
        input_script = workspace / str(input_script_raw) if input_script_raw else None
        save_ram_raw = item.get("save_ram")
        save_ram_sha256_raw = item.get("save_ram_sha256")
        if bool(save_ram_raw) != bool(save_ram_sha256_raw):
            raise ValueError(
                f"scenario {scenario_id} must specify save_ram and save_ram_sha256 together"
            )
        save_ram = workspace / str(save_ram_raw) if save_ram_raw else None
        save_ram_sha256 = str(save_ram_sha256_raw).lower() if save_ram_sha256_raw else None
        if save_ram_sha256 is not None and not re.fullmatch(r"[0-9a-f]{64}", save_ram_sha256):
            raise ValueError(f"scenario {scenario_id} has invalid save RAM SHA-256")
        scenarios.append(
            scenario_t(
                id=scenario_id,
                suite=suite,
                rom=rom,
                sha256=sha256,
                input_script=input_script,
                save_ram=save_ram,
                save_ram_sha256=save_ram_sha256,
                bsnes_input_frame_offset=bsnes_input_frame_offset,
                start_frame=start_frame,
                end_frame=end_frame,
                step_limit=step_limit,
                compare_profile=compare_profile,
            )
        )
        seen_ids.add(scenario_id)
    return scenarios


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def find_build_dir(workspace: Path, requested: str | None) -> Path:
    candidates = [Path(requested)] if requested else [
        Path("build"),
        Path("cmake-build-sdl-release"),
        Path("cmake-build-debug"),
    ]
    for candidate in candidates:
        resolved = candidate if candidate.is_absolute() else workspace / candidate
        if all((resolved / tool).exists() for tool in REQUIRED_TOOLS):
            return resolved
    labels = ", ".join(str(candidate) for candidate in candidates)
    raise ValueError(f"no build directory with accuracy-fence tools found in: {labels}")


def resolve_path(workspace: Path, raw: str | Path) -> Path:
    path = Path(raw)
    return path if path.is_absolute() else (workspace / path).resolve()


def command_text(command: list[str]) -> str:
    return " ".join(subprocess.list2cmdline([part]) for part in command)


def write_shifted_input_script(source: Path, destination: Path, frame_offset: int) -> None:
    raw = source.read_text(encoding="utf-8").strip()
    shifted: list[str] = []
    if raw:
        for entry in raw.split(","):
            match = re.fullmatch(r"(\d+)-(\d+)=([0-9a-fA-F]{4})", entry)
            if match is None:
                raise ValueError(f"invalid joypad input entry in {source}: {entry!r}")
            start_frame = int(match.group(1)) + frame_offset
            end_frame = int(match.group(2)) + frame_offset
            if start_frame < 1 or end_frame < start_frame:
                raise ValueError(f"input frame offset moves {entry!r} outside the valid range")
            shifted.append(f"{start_frame}-{end_frame}={match.group(3).lower()}")
    destination.write_text(",".join(shifted) + "\n", encoding="utf-8")


def run_logged(command: list[str], cwd: Path, env: dict[str, str], log_path: Path) -> tuple[int, str]:
    print(f"$ {command_text(command)}", flush=True)
    try:
        result = subprocess.run(
            command,
            cwd=cwd,
            env=env,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
        )
        return_code = result.returncode
        output = result.stdout or ""
    except OSError as error:
        return_code = 127
        output = f"Unable to run command: {error}\n"
    log_path.write_text(output, encoding="utf-8")
    return return_code, output


def emit_failure(label: str, output: str, log_path: Path) -> None:
    print(f"{label} failed; full log: {log_path}", file=sys.stderr)
    lines = output.rstrip().splitlines()
    if lines:
        print("\n".join(lines[-80:]), file=sys.stderr)


def validate_clover_summary(output: str, scenario: scenario_t, expected_dumps: int) -> str | None:
    run_match = re.search(
        r"Run: target_frames=(\d+) frames_completed=(\d+).*step_limit_hit=(\d+)", output
    )
    diagnostics_match = re.search(
        r"Diagnostics: terminal_pc=(\d+) cpu_placeholder_opcodes=(\d+)", output
    )
    dumps_match = re.search(r"Frame dumps: .* dumped=(\d+) start_frame=(\d+)", output)
    if run_match is None or diagnostics_match is None or dumps_match is None:
        return "Clover summary is missing required guardrail fields"
    if tuple(int(value) for value in run_match.groups()) != (
        scenario.end_frame,
        scenario.end_frame,
        0,
    ):
        return f"Clover did not reach frame {scenario.end_frame} cleanly"
    if tuple(int(value) for value in diagnostics_match.groups()) != (0, 0):
        return "Clover reported terminal PC or placeholder opcode execution"
    if tuple(int(value) for value in dumps_match.groups()) != (
        expected_dumps,
        scenario.start_frame,
    ):
        return "Clover did not write the requested frame range"
    return None


def validate_bsnes_summary(output: str, scenario: scenario_t, expected_dumps: int) -> str | None:
    run_match = re.search(r"bsnes run: frames_completed=(\d+)", output)
    dumps_match = re.search(r"Frame dumps: .* dumped=(\d+) start_frame=(\d+)", output)
    if run_match is None or dumps_match is None:
        return "bsnes summary is missing required fields"
    if int(run_match.group(1)) != scenario.end_frame:
        return f"bsnes did not reach frame {scenario.end_frame}"
    if tuple(int(value) for value in dumps_match.groups()) != (
        expected_dumps,
        scenario.start_frame,
    ):
        return "bsnes did not write the requested frame range"
    return None


def run_scenario(
    scenario: scenario_t,
    workspace: Path,
    build_dir: Path,
    bsnes_core: Path,
    output_root: Path,
    dry_run: bool,
) -> bool:
    print(
        f"\n[{scenario.id}] suite={scenario.suite} "
        f"frames={scenario.start_frame}-{scenario.end_frame}",
        flush=True,
    )
    if not scenario.rom.exists():
        print(f"Missing local ROM: {scenario.rom}", file=sys.stderr)
        return False
    actual_sha256 = sha256_file(scenario.rom)
    if actual_sha256 != scenario.sha256:
        print(
            f"ROM identity mismatch: {scenario.rom}\n"
            f"  expected {scenario.sha256}\n  actual   {actual_sha256}",
            file=sys.stderr,
        )
        return False
    if scenario.input_script is not None and not scenario.input_script.exists():
        print(f"Missing input script: {scenario.input_script}", file=sys.stderr)
        return False
    if scenario.save_ram is not None:
        if not scenario.save_ram.exists():
            print(f"Missing save RAM: {scenario.save_ram}", file=sys.stderr)
            return False
        actual_save_sha256 = sha256_file(scenario.save_ram)
        if actual_save_sha256 != scenario.save_ram_sha256:
            print(
                f"Save RAM identity mismatch: {scenario.save_ram}\n"
                f"  expected {scenario.save_ram_sha256}\n  actual   {actual_save_sha256}",
                file=sys.stderr,
            )
            return False

    scenario_dir = output_root / scenario.id
    bsnes_dir = scenario_dir / "bsnes"
    clover_dir = scenario_dir / "clover"
    expected_dumps = scenario.end_frame - scenario.start_frame + 1
    bsnes_command = [
        str(build_dir / "clover_bsnes_bringup"),
        str(scenario.rom),
        str(scenario.end_frame),
        str(bsnes_dir),
        str(expected_dumps),
        str(scenario.start_frame),
        str(bsnes_core),
    ]
    clover_command = [
        str(build_dir / "clover_rom_bringup"),
        str(scenario.rom),
        str(scenario.end_frame),
        str(scenario.step_limit),
        str(clover_dir),
        str(expected_dumps),
        str(scenario.start_frame),
    ]
    compare_command = [
        str(build_dir / "clover_frame_range_compare"),
        str(bsnes_dir),
        str(clover_dir),
        str(scenario.start_frame),
        str(scenario.end_frame),
        "0",
        "0",
        scenario.compare_profile,
    ]
    if dry_run:
        if scenario.input_script is not None:
            print(
                f"Input: {scenario.input_script} "
                f"(bsnes frame offset {scenario.bsnes_input_frame_offset:+d})"
            )
        if scenario.save_ram is not None:
            print(f"Save RAM: {scenario.save_ram} ({scenario.save_ram_sha256})")
        for command in (bsnes_command, clover_command, compare_command):
            print(f"$ {command_text(command)}")
        return True

    scenario_dir.mkdir(parents=True, exist_ok=False)
    bsnes_env = os.environ.copy()
    bsnes_env["CLOVER_BSNES_ENTROPY"] = "None"
    clover_env = os.environ.copy()
    clover_env["CLOVER_STARTUP_ENTROPY"] = "none"
    clover_env["CLOVER_BRINGUP_VERBOSE"] = "0"
    clover_env["CLOVER_DUMP_FRAMES_ONLY"] = "1"
    if scenario.save_ram is not None:
        clover_env["CLOVER_SAVE_RAM_FILE"] = str(scenario.save_ram)
        bsnes_env["CLOVER_SAVE_RAM_FILE"] = str(scenario.save_ram)
    if scenario.input_script is not None:
        clover_env["CLOVER_JOYPAD1_SCRIPT_FILE"] = str(scenario.input_script)
        if scenario.bsnes_input_frame_offset == 0:
            bsnes_input_script = scenario.input_script
        else:
            bsnes_input_script = scenario_dir / "bsnes.joypad1.script"
            try:
                write_shifted_input_script(
                    scenario.input_script,
                    bsnes_input_script,
                    scenario.bsnes_input_frame_offset,
                )
            except (OSError, ValueError) as error:
                print(f"Unable to prepare bsnes input script: {error}", file=sys.stderr)
                return False
        bsnes_env["CLOVER_JOYPAD1_SCRIPT_FILE"] = str(bsnes_input_script)

    return_code, output = run_logged(
        bsnes_command, workspace, bsnes_env, scenario_dir / "bsnes.log"
    )
    if return_code != 0:
        emit_failure("bsnes capture", output, scenario_dir / "bsnes.log")
        return False
    summary_error = validate_bsnes_summary(output, scenario, expected_dumps)
    if summary_error is not None:
        emit_failure(summary_error, output, scenario_dir / "bsnes.log")
        return False

    return_code, output = run_logged(
        clover_command, workspace, clover_env, scenario_dir / "clover.log"
    )
    if return_code != 0:
        emit_failure("Clover capture", output, scenario_dir / "clover.log")
        return False
    summary_error = validate_clover_summary(output, scenario, expected_dumps)
    if summary_error is not None:
        emit_failure(summary_error, output, scenario_dir / "clover.log")
        return False

    return_code, output = run_logged(
        compare_command, workspace, os.environ.copy(), scenario_dir / "compare.log"
    )
    if return_code != 0:
        emit_failure("exact frame comparison", output, scenario_dir / "compare.log")
        return False
    print(f"PASS {scenario.id}: {expected_dumps} exact frames", flush=True)
    return True


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Run Clover's manifest-driven exact accuracy regression fence."
    )
    parser.add_argument("--manifest", default=str(DEFAULT_MANIFEST))
    parser.add_argument(
        "--suite",
        choices=("baseline", "interactive", "full", "investigation"),
        default="full",
    )
    parser.add_argument("--scenario", action="append", default=[], help="Run only this scenario id; repeatable")
    parser.add_argument("--build-dir", help="Build directory containing the bringup and compare tools")
    parser.add_argument("--bsnes-core", default=str(DEFAULT_BSNES_CORE))
    parser.add_argument("--output-dir", help="Preserve artifacts in this new directory")
    parser.add_argument("--keep-artifacts", action="store_true", help="Keep the temporary output even on success")
    parser.add_argument("--skip-tests", action="store_true", help="Skip the CTest preflight")
    parser.add_argument("--dry-run", action="store_true", help="Validate inputs and print commands without running")
    parser.add_argument("--list", action="store_true", help="List configured scenarios and exit")
    args = parser.parse_args()

    workspace = Path(__file__).resolve().parent.parent
    manifest_path = resolve_path(workspace, args.manifest)
    try:
        scenarios = load_manifest(manifest_path, workspace)
    except ValueError as error:
        print(f"Accuracy fence configuration error: {error}", file=sys.stderr)
        return 1

    if args.list:
        for scenario in scenarios:
            print(
                f"{scenario.id:32} {scenario.suite:11} "
                f"frames {scenario.start_frame}-{scenario.end_frame}"
            )
        return 0

    try:
        build_dir = find_build_dir(workspace, args.build_dir)
        bsnes_core = resolve_path(workspace, args.bsnes_core)
    except ValueError as error:
        print(f"Accuracy fence configuration error: {error}", file=sys.stderr)
        return 1

    selected_ids = set(args.scenario)
    if selected_ids:
        known_ids = {scenario.id for scenario in scenarios}
        unknown_ids = selected_ids - known_ids
        if unknown_ids:
            print(f"Unknown scenario ids: {', '.join(sorted(unknown_ids))}", file=sys.stderr)
            return 1
        selected = [scenario for scenario in scenarios if scenario.id in selected_ids]
    else:
        selected_suites = (
            {"baseline", "interactive"} if args.suite == "full" else {args.suite}
        )
        selected = [scenario for scenario in scenarios if scenario.suite in selected_suites]
    if not selected:
        print("No scenarios selected.", file=sys.stderr)
        return 1
    if not bsnes_core.exists():
        print(f"bsnes core not found: {bsnes_core}", file=sys.stderr)
        return 1

    if args.output_dir:
        output_root = resolve_path(workspace, args.output_dir)
        if output_root.exists():
            print(f"Output directory already exists: {output_root}", file=sys.stderr)
            return 1
        output_root.mkdir(parents=True)
        temporary_output = False
    else:
        output_root = Path(tempfile.mkdtemp(prefix="clover-accuracy-fence-", dir="/private/tmp"))
        temporary_output = True

    print(f"Manifest: {manifest_path}")
    print(f"Build: {build_dir}")
    print(f"bsnes: {bsnes_core}")
    print(f"Artifacts: {output_root}")

    passed = True
    if not args.skip_tests and not args.dry_run:
        print("\n[preflight] CTest", flush=True)
        preflight_log = output_root / "ctest.log"
        return_code, output = run_logged(
            ["ctest", "--test-dir", str(build_dir), "--output-on-failure"],
            workspace,
            os.environ.copy(),
            preflight_log,
        )
        if return_code != 0:
            emit_failure("CTest preflight", output, preflight_log)
            passed = False

    if passed:
        for scenario in selected:
            if not run_scenario(
                scenario, workspace, build_dir, bsnes_core, output_root, args.dry_run
            ):
                passed = False
                break

    if passed:
        result_label = "Accuracy fence dry run passed" if args.dry_run else "Accuracy fence passed"
        print(f"\n{result_label}: {len(selected)} scenario(s).")
        if temporary_output and not args.keep_artifacts:
            shutil.rmtree(output_root)
            print("Temporary frame artifacts removed.")
        else:
            print(f"Artifacts preserved: {output_root}")
        return 0

    print(f"\nAccuracy fence FAILED. Artifacts preserved: {output_root}", file=sys.stderr)
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
