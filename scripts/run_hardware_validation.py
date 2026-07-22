#!/usr/bin/env python3

from __future__ import annotations

import argparse
import concurrent.futures
import hashlib
import json
import os
import re
import subprocess
import sys
import tempfile
import zipfile
from dataclasses import asdict, dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Any


DEFAULT_MANIFEST = Path("validation/hardware_tests.json")
DEFAULT_BSNES_CORE = Path("../bsnes/bsnes/out/bsnes_libretro.dylib")
REQUIRED_TOOLS = ("clover_rom_bringup", "clover_bsnes_bringup", "clover_frame_compare")


@dataclass(frozen=True)
class test_t:
    id: str
    collection: str
    suites: tuple[str, ...]
    category: str
    method: str
    rom: Path | None
    archive: Path | None
    member: str | None
    sha256: str
    frame: int
    step_limit: int
    automation: str
    input_script: Path | None
    reset_frames: tuple[int, ...]
    hardware_profiles: tuple[str, ...]
    reference: Path | None
    notes: str


@dataclass
class result_t:
    id: str
    collection: str
    category: str
    method: str
    frame: int
    hardware_profiles: list[str]
    selected_profile: str
    status: str
    clover_ok: bool = False
    self_report: str | None = None
    bsnes_ok: bool | None = None
    reference_compared: bool = False
    reference_exact: bool | None = None
    bsnes_compared: bool = False
    bsnes_exact: bool | None = None
    differing_pixels: int | None = None
    first_difference: str | None = None
    artifact_directory: str = ""
    notes: str = ""
    error: str | None = None


def resolve_path(workspace: Path, raw: str | Path) -> Path:
    path = Path(raw)
    return path if path.is_absolute() else (workspace / path).resolve()


def load_profile_catalog(path: Path) -> tuple[str, dict[str, dict[str, Any]]]:
    try:
        raw: dict[str, Any] = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise ValueError(f"unable to read manifest {path}: {error}") from error
    hardware_model = raw.get("hardware_model")
    if not isinstance(hardware_model, dict):
        raise ValueError("hardware-test manifest is missing hardware_model")
    canonical = hardware_model.get("canonical")
    profiles = hardware_model.get("profiles")
    if not isinstance(canonical, str) or not isinstance(profiles, dict) or canonical not in profiles:
        raise ValueError("hardware-test manifest has an invalid hardware profile catalog")
    return canonical, profiles


def load_manifest(path: Path, workspace: Path) -> list[test_t]:
    try:
        raw: dict[str, Any] = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise ValueError(f"unable to read manifest {path}: {error}") from error
    if raw.get("version") != 1:
        raise ValueError("hardware-test manifest version must be 1")

    provenance = raw.get("provenance")
    if not isinstance(provenance, dict) or not provenance:
        raise ValueError("hardware-test manifest must contain collection provenance")
    known_collections = {
        str(collection) for collection, source in provenance.items()
        if isinstance(collection, str) and collection and source
    }
    if len(known_collections) != len(provenance):
        raise ValueError("hardware-test manifest has invalid collection provenance")

    defaults = raw.get("defaults", {})
    items = raw.get("tests")
    if not isinstance(items, list) or not items:
        raise ValueError("hardware-test manifest must contain tests")

    tests: list[test_t] = []
    seen: set[str] = set()
    for index, item in enumerate(items):
        if not isinstance(item, dict):
            raise ValueError(f"test {index} must be an object")
        try:
            test_id = str(item["id"])
            collection = str(item["collection"])
            suites = tuple(str(value) for value in item.get("suites", defaults["suites"]))
            category = str(item["category"])
            method = str(item.get("method", defaults["method"]))
            sha256 = str(item["sha256"]).lower()
            frame = int(item.get("frame", defaults["frame"]))
            step_limit = int(item.get("step_limit", defaults["step_limit"]))
            automation = str(item.get("automation", "power-on"))
            input_script_raw = item.get("input_script")
            reset_frames = tuple(int(value) for value in item.get("reset_frames", ()))
            hardware_profiles = tuple(str(value) for value in item.get("hardware_profiles", ("unknown",)))
            notes = str(item.get("notes", ""))
        except (KeyError, TypeError, ValueError) as error:
            raise ValueError(f"test {index} has an invalid or missing field: {error}") from error

        if not test_id or test_id in seen:
            raise ValueError(f"test id is empty or duplicated: {test_id!r}")
        if collection not in known_collections:
            raise ValueError(
                f"test {test_id} uses collection {collection!r} without provenance"
            )
        if method not in {"self-report", "digital-reference", "characterization"}:
            raise ValueError(f"test {test_id} has unknown method {method!r}")
        if automation not in {"power-on", "controller-script", "reset-script", "manual"}:
            raise ValueError(f"test {test_id} has unknown automation {automation!r}")
        if any(frame_number < 1 or frame_number > frame for frame_number in reset_frames):
            raise ValueError(f"test {test_id} has a reset frame outside its run")
        if automation == "controller-script" and not input_script_raw:
            raise ValueError(f"test {test_id} controller automation is missing input_script")
        if automation == "reset-script" and not reset_frames:
            raise ValueError(f"test {test_id} reset automation is missing reset_frames")
        if frame < 1 or step_limit < 1:
            raise ValueError(f"test {test_id} has invalid limits")
        if not re.fullmatch(r"[0-9a-f]{64}", sha256):
            raise ValueError(f"test {test_id} has invalid SHA-256")

        rom_raw = item.get("rom")
        archive_raw = item.get("archive")
        member_raw = item.get("member")
        if bool(rom_raw) == bool(archive_raw):
            raise ValueError(f"test {test_id} must specify exactly one of rom or archive")
        if archive_raw and not member_raw:
            raise ValueError(f"test {test_id} archive is missing member")

        reference_raw = item.get("reference")
        if method == "digital-reference" and not reference_raw:
            raise ValueError(f"test {test_id} uses digital-reference without a reference file")
        tests.append(test_t(
            id=test_id,
            collection=collection,
            suites=suites,
            category=category,
            method=method,
            rom=resolve_path(workspace, str(rom_raw)) if rom_raw else None,
            archive=resolve_path(workspace, str(archive_raw)) if archive_raw else None,
            member=str(member_raw) if member_raw else None,
            sha256=sha256,
            frame=frame,
            step_limit=step_limit,
            automation=automation,
            input_script=resolve_path(workspace, str(input_script_raw)) if input_script_raw else None,
            reset_frames=reset_frames,
            hardware_profiles=hardware_profiles,
            reference=resolve_path(workspace, str(reference_raw)) if reference_raw else None,
            notes=notes,
        ))
        seen.add(test_id)
    return tests


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def stage_rom(test: test_t, scenario_dir: Path) -> Path:
    if test.rom is not None:
        if not test.rom.exists():
            raise ValueError(f"missing ROM: {test.rom}")
        data = test.rom.read_bytes()
        source_label = str(test.rom)
        staged = test.rom
    else:
        assert test.archive is not None and test.member is not None
        if not test.archive.exists():
            raise ValueError(f"missing archive: {test.archive}")
        try:
            with zipfile.ZipFile(test.archive) as archive:
                data = archive.read(test.member)
        except (OSError, KeyError, zipfile.BadZipFile) as error:
            raise ValueError(f"unable to read {test.member} from {test.archive}: {error}") from error
        source_label = f"{test.archive}!{test.member}"
        suffix = Path(test.member).suffix or ".sfc"
        staged = scenario_dir / f"input{suffix}"
        staged.write_bytes(data)

    actual = sha256_bytes(data)
    if actual != test.sha256:
        raise ValueError(
            f"ROM identity mismatch for {source_label}: expected {test.sha256}, actual {actual}"
        )
    return staged


def find_build_dir(workspace: Path, requested: str | None) -> Path:
    candidates = [Path(requested)] if requested else [
        Path("build"), Path("cmake-build-sdl-release"), Path("cmake-build-debug")
    ]
    for candidate in candidates:
        resolved = candidate if candidate.is_absolute() else workspace / candidate
        if all((resolved / tool).exists() for tool in REQUIRED_TOOLS):
            return resolved
    raise ValueError("no build directory containing the hardware-validation tools was found")


def run_logged(command: list[str], cwd: Path, env: dict[str, str], log: Path) -> tuple[int, str]:
    try:
        completed = subprocess.run(
            command, cwd=cwd, env=env, text=True,
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        )
        output = completed.stdout or ""
        code = completed.returncode
    except OSError as error:
        output = f"Unable to run command: {error}\n"
        code = 127
    log.write_text(output, encoding="utf-8")
    return code, output


def validate_clover(output: str, frame: int) -> str | None:
    run = re.search(r"Run: target_frames=(\d+) frames_completed=(\d+).*step_limit_hit=(\d+)", output)
    diagnostics = re.search(r"Diagnostics: terminal_pc=(\d+) cpu_placeholder_opcodes=(\d+)", output)
    dumps = re.search(r"Frame dumps: .* dumped=(\d+) start_frame=(\d+)", output)
    if run is None or diagnostics is None or dumps is None:
        return "Clover summary is missing guardrail fields"
    target, completed, limit_hit = (int(value) for value in run.groups())
    if target != frame or completed != frame or limit_hit != 0:
        return f"Clover did not reach frame {frame} cleanly"
    terminal, placeholders = (int(value) for value in diagnostics.groups())
    if terminal != 0 or placeholders != 0:
        return f"Clover guardrail failure: terminal_pc={terminal}, placeholder_opcodes={placeholders}"
    if tuple(int(value) for value in dumps.groups()) != (1, frame):
        return "Clover did not capture the requested observation frame"
    return None


def validate_bsnes(output: str, frame: int) -> str | None:
    run = re.search(r"bsnes run: frames_completed=(\d+)", output)
    dumps = re.search(r"Frame dumps: .* dumped=(\d+) start_frame=(\d+)", output)
    if run is None or dumps is None:
        return "bsnes summary is missing required fields"
    if int(run.group(1)) != frame or tuple(int(value) for value in dumps.groups()) != (1, frame):
        return f"bsnes did not capture frame {frame} cleanly"
    return None


def parse_compare(output: str) -> tuple[bool, int | None, str | None]:
    exact = "Frames match exactly." in output
    pixels_match = re.search(r"differing_pixels=(\d+)", output)
    first_match = re.search(r"First difference: x=(\d+) y=(\d+)", output)
    size_match = re.search(r"Frame size mismatch: expected=(\d+x\d+) actual=(\d+x\d+)", output)
    pixels = int(pixels_match.group(1)) if pixels_match else None
    if first_match:
        first = f"({first_match.group(1)},{first_match.group(2)})"
    elif size_match:
        first = f"size expected={size_match.group(1)} actual={size_match.group(2)}"
    else:
        first = None
    return exact, pixels, first


SELF_REPORT_SIGNATURES = {
    "passed": ((
        "######......................................##..",
        "##...##.....................................##..",
        "##...##..####....####....####....####....#####..",
        "######......##..##......##......##..##..##..##..",
        "##.......#####...####....####...######..##..##..",
        "##......##..##......##......##..##......##..##..",
        "##.......###.##.#####...#####....####....#####..",
    ), (
        "#####.....##.....####....####...######..####....",
        "##..##...####...##..##..##..##..##......##.##...",
        "##..##..##..##..##......##......##......##..##..",
        "#####...##..##...####....####...#####...##..##..",
        "##......######......##......##..##......##..##..",
        "##......##..##..##..##..##..##..##......##.##...",
        "##......##..##...####....####...######..####....",
    ), (
        ".....####.............................................................",
        "....##..##............................................................",
        "....###.....##..##...####....####....####....#####...#####............",
        ".....###....##..##..##..##..##..##..##..##..##......##................",
        ".......###..##..##..##......##......######...####....####.............",
        "....##..##..##..##..##..##..##..##..##..........##......##............",
        ".....####....###.##..####....####....####...#####...#####.............",
    )),
    "failed": ((
        "######............##.....###................##..",
        "##........................##................##..",
        "##.......####....###......##.....####....#####..",
        "#####.......##....##......##....##..##..##..##..",
        "##.......#####....##......##....######..##..##..",
        "##......##..##....##......##....##......##..##..",
        "##.......#####...####....####....####....#####..",
    ), (
        "#######...................##................##..",
        "##................##......##................##..",
        "##.......####.............##.....####....#####..",
        "######......##....##......##....##..##..##..##..",
        "##.......#####....##......##....######..##..##..",
        "##......##..##....##......##....##......##..##..",
        "##.......###.##...##......##.....####....#####..",
    ), (
        "....#######...........##.....###...............###..........",
        ".....##...#...................##................##..........",
        ".....##.#....####....###......##.....####.......##..........",
        ".....####.......##....##......##....##..##...#####..........",
        ".....##.#....#####....##......##....######..##..##..........",
        ".....##.....##..##....##......##....##......##..##..........",
        "....####.....###.##..####....####....####....###.##.........",
    )),
}


def decode_self_report(frame: Path) -> str | None:
    """Recognize stable pass/fail banners used by the curated ROM collections."""
    try:
        data = frame.read_bytes()
        match = re.match(rb"P6\s+(\d+)\s+(\d+)\s+255\s", data)
        if match is None:
            return None
        width, height = (int(value) for value in match.groups())
        pixels = data[match.end():]
        if len(pixels) != width * height * 3:
            return None
    except OSError:
        return None

    bright = [sum(pixels[index:index + 3]) > 400 for index in range(0, len(pixels), 3)]
    for verdict in ("failed", "passed"):
        for signature in SELF_REPORT_SIGNATURES[verdict]:
            signature_width = len(signature[0])
            for y in range(height - len(signature) + 1):
                for x in range(width - signature_width + 1):
                    if all(
                        bright[(y + row) * width + x + column] == (value == "#")
                        for row, values in enumerate(signature)
                        for column, value in enumerate(values)
                    ):
                        return verdict
    return None


def compare_frames(tool: Path, expected: Path, actual: Path, workspace: Path, log: Path) -> tuple[bool, int | None, str | None]:
    _, output = run_logged([str(tool), str(expected), str(actual), "exact"], workspace, os.environ.copy(), log)
    return parse_compare(output)


def run_test(
    test: test_t,
    workspace: Path,
    build_dir: Path,
    bsnes_core: Path,
    output_root: Path,
    clover_only: bool,
    selected_profile: str,
) -> result_t:
    scenario_dir = output_root / test.id
    scenario_dir.mkdir(parents=True, exist_ok=False)
    result = result_t(
        id=test.id, collection=test.collection, category=test.category,
        method=test.method, frame=test.frame,
        hardware_profiles=list(test.hardware_profiles), selected_profile=selected_profile,
        status="OBSERVED",
        artifact_directory=str(scenario_dir), notes=test.notes,
    )

    if test.automation == "manual":
        result.status = "NEEDS_AUTOMATION"
        result.error = f"requires {test.automation}"
        return result

    try:
        rom = stage_rom(test, scenario_dir)
    except ValueError as error:
        result.status = "ERROR"
        result.error = str(error)
        return result

    clover_dir = scenario_dir / "clover"
    clover_env = os.environ.copy()
    clover_env.update({
        "CLOVER_STARTUP_ENTROPY": "none",
        "CLOVER_BRINGUP_VERBOSE": "0",
        "CLOVER_DUMP_FRAMES_ONLY": "1",
        "CLOVER_SNES_HARDWARE_PROFILE": selected_profile,
    })
    if test.input_script is not None:
        if not test.input_script.exists():
            result.status = "ERROR"
            result.error = f"missing input script: {test.input_script}"
            return result
        clover_env["CLOVER_JOYPAD1_SCRIPT_FILE"] = str(test.input_script)
    if test.reset_frames:
        clover_env["CLOVER_RESET_FRAMES"] = ",".join(str(value) for value in test.reset_frames)
    clover_command = [
        str(build_dir / "clover_rom_bringup"), str(rom), str(test.frame),
        str(test.step_limit), str(clover_dir), "1", str(test.frame),
    ]
    code, output = run_logged(clover_command, workspace, clover_env, scenario_dir / "clover.log")
    error = "Clover process failed" if code != 0 else validate_clover(output, test.frame)
    if error is not None:
        result.status = "ERROR"
        result.error = error
        return result
    result.clover_ok = True
    clover_frame = clover_dir / f"frame_{test.frame}.ppm"
    if test.method == "self-report":
        result.self_report = decode_self_report(clover_frame)
        if result.self_report == "passed":
            result.status = "SELF_REPORT_PASS"
        elif result.self_report == "failed":
            result.status = "SELF_REPORT_FAIL"
        else:
            result.status = "SELF_REPORT_PENDING"

    if test.reference is not None:
        result.reference_compared = True
        if not test.reference.exists():
            result.status = "MISSING_REFERENCE"
            result.error = f"missing hardware reference: {test.reference}"
            return result
        exact, pixels, first = compare_frames(
            build_dir / "clover_frame_compare", test.reference, clover_frame,
            workspace, scenario_dir / "hardware-compare.log",
        )
        result.reference_exact = exact
        result.differing_pixels = pixels
        result.first_difference = first
        result.status = "VERIFIED" if exact else "HARDWARE_DIFFERENCE"

    if not clover_only:
        bsnes_dir = scenario_dir / "bsnes"
        bsnes_env = os.environ.copy()
        bsnes_env["CLOVER_BSNES_ENTROPY"] = "None"
        if test.input_script is not None:
            bsnes_env["CLOVER_JOYPAD1_SCRIPT_FILE"] = str(test.input_script)
        if test.reset_frames:
            bsnes_env["CLOVER_RESET_FRAMES"] = ",".join(str(value) for value in test.reset_frames)
        command = [
            str(build_dir / "clover_bsnes_bringup"), str(rom), str(test.frame),
            str(bsnes_dir), "1", str(test.frame), str(bsnes_core),
        ]
        code, output = run_logged(command, workspace, bsnes_env, scenario_dir / "bsnes.log")
        error = "bsnes process failed" if code != 0 else validate_bsnes(output, test.frame)
        if error is not None:
            result.bsnes_ok = False
            if result.status == "OBSERVED":
                result.status = "BSNES_ERROR"
            result.error = error
            return result
        result.bsnes_ok = True
        result.bsnes_compared = True
        exact, pixels, first = compare_frames(
            build_dir / "clover_frame_compare",
            bsnes_dir / f"frame_{test.frame}.ppm", clover_frame,
            workspace, scenario_dir / "bsnes-compare.log",
        )
        result.bsnes_exact = exact
        if not result.reference_compared:
            result.differing_pixels = pixels
            result.first_difference = first
            if test.method != "self-report" and exact:
                result.status = "BSNES_MATCH"
            elif test.method != "self-report":
                result.status = "BSNES_DIFFERENCE"

    if clover_only and not result.reference_compared:
        if test.method != "self-report":
            result.status = "OBSERVED"
    return result


def write_reports(output_root: Path, manifest: Path, selected_profile: str,
                  results: list[result_t]) -> None:
    counts: dict[str, int] = {}
    for result in results:
        counts[result.status] = counts.get(result.status, 0) + 1
    payload = {
        "version": 1,
        "generated_at": datetime.now(timezone.utc).isoformat(),
        "manifest": str(manifest),
        "selected_profile": selected_profile,
        "summary": counts,
        "results": [asdict(result) for result in results],
    }
    (output_root / "report.json").write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")

    lines = [
        "# Clover SNES Hardware Validation Report", "",
        f"Generated: {payload['generated_at']}", "",
        f"Selected hardware profile: `{selected_profile}`", "",
        "## Summary", "",
    ]
    for status, count in sorted(counts.items()):
        lines.append(f"- {status}: {count}")
    lines.extend([
        "", "## Results", "",
        "| Test | Collection | Category | Method | Status | Self-report | bsnes | Pixels | Hardware profile |",
        "|---|---|---|---|---|---|---:|---:|---|",
    ])
    for result in results:
        bsnes = "—" if result.bsnes_exact is None else ("exact" if result.bsnes_exact else "different")
        pixels = "—" if result.differing_pixels is None else str(result.differing_pixels)
        profiles = ", ".join(result.hardware_profiles)
        lines.append(
            f"| {result.id} | {result.collection} | {result.category} | {result.method} | "
            f"{result.status} | {result.self_report or '—'} | {bsnes} | {pixels} | {profiles} |"
        )
    lines.extend([
        "", "## Interpretation", "",
        "`VERIFIED` requires an approved real-hardware reference. `BSNES_MATCH` is useful differential evidence, "
        "not proof of hardware correctness. `SELF_REPORT_PASS` and `SELF_REPORT_FAIL` are decoded from the "
        "test ROM's own result screen; `SELF_REPORT_PENDING` means no terminal result was visible yet. "
        "`NOT_APPLICABLE` means the test targets a different physical revision and was intentionally not run. "
        "Characterization and hardware-revision-specific tests must not be promoted without provenance.", "",
    ])
    (output_root / "report.md").write_text("\n".join(lines), encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description="Run Clover's SNES hardware-validation laboratory.")
    parser.add_argument("--manifest", default=str(DEFAULT_MANIFEST))
    parser.add_argument("--hardware-profile", help="SNES hardware profile (defaults to manifest canonical)")
    parser.add_argument("--suite", default="smoke", help="smoke, all, collection, or manifest suite tag")
    parser.add_argument("--scenario", action="append", default=[])
    parser.add_argument("--build-dir")
    parser.add_argument("--bsnes-core", default=str(DEFAULT_BSNES_CORE))
    parser.add_argument("--output-dir")
    parser.add_argument("--jobs", type=int, default=4)
    parser.add_argument("--clover-only", action="store_true")
    parser.add_argument("--skip-tests", action="store_true")
    parser.add_argument("--strict", action="store_true", help="Fail unless every selected test is VERIFIED")
    parser.add_argument("--list", action="store_true")
    args = parser.parse_args()

    workspace = Path(__file__).resolve().parent.parent
    manifest = resolve_path(workspace, args.manifest)
    try:
        canonical_profile, profile_catalog = load_profile_catalog(manifest)
        tests = load_manifest(manifest, workspace)
    except ValueError as error:
        print(f"Hardware validation configuration error: {error}", file=sys.stderr)
        return 1

    selected_profile = args.hardware_profile or canonical_profile
    profile_definition = profile_catalog.get(selected_profile)
    if profile_definition is None:
        print(f"Unknown SNES hardware profile: {selected_profile}", file=sys.stderr)
        return 1
    if not bool(profile_definition.get("implemented", False)):
        print(f"SNES hardware profile is recognized but not implemented: {selected_profile}", file=sys.stderr)
        return 1

    selected_ids = set(args.scenario)
    if selected_ids:
        unknown = selected_ids - {test.id for test in tests}
        if unknown:
            print(f"Unknown scenarios: {', '.join(sorted(unknown))}", file=sys.stderr)
            return 1
        selected = [test for test in tests if test.id in selected_ids]
    elif args.suite == "all":
        selected = tests
    else:
        selected = [
            test for test in tests
            if args.suite == test.collection or args.suite in test.suites
        ]
    if not selected:
        print(f"No tests selected for suite {args.suite!r}", file=sys.stderr)
        return 1

    if args.list:
        for test in selected:
            print(
                f"{test.id:40} {test.collection:13} {test.category:18} "
                f"{test.method:17} frame={test.frame:5} automation={test.automation} "
                f"profiles={','.join(test.hardware_profiles)}"
            )
        return 0

    try:
        build_dir = find_build_dir(workspace, args.build_dir)
    except ValueError as error:
        print(f"Hardware validation configuration error: {error}", file=sys.stderr)
        return 1

    bsnes_core = resolve_path(workspace, args.bsnes_core)
    if not args.clover_only and not bsnes_core.exists():
        print(f"bsnes core not found: {bsnes_core}", file=sys.stderr)
        return 1
    if args.jobs < 1 or args.jobs > 32:
        print("--jobs must be between 1 and 32", file=sys.stderr)
        return 1

    if args.output_dir:
        output_root = resolve_path(workspace, args.output_dir)
        if output_root.exists():
            print(f"Output directory already exists: {output_root}", file=sys.stderr)
            return 1
        output_root.mkdir(parents=True)
    else:
        output_root = Path(tempfile.mkdtemp(prefix="clover-hardware-validation-", dir="/private/tmp"))

    print(f"Manifest: {manifest}")
    print(f"Build: {build_dir}")
    print(f"Artifacts: {output_root}")
    print(f"Hardware profile: {selected_profile}")
    print(f"Selected: {len(selected)} tests; jobs={args.jobs}")

    if not args.skip_tests:
        completed = subprocess.run(
            ["ctest", "--test-dir", str(build_dir), "--output-on-failure"],
            cwd=workspace,
        )
        if completed.returncode != 0:
            print(f"CTest preflight failed; artifacts preserved: {output_root}", file=sys.stderr)
            return 1

    results: list[result_t] = [
        result_t(
            id=test.id, collection=test.collection, category=test.category,
            method=test.method, frame=test.frame,
            hardware_profiles=list(test.hardware_profiles), selected_profile=selected_profile,
            status="NOT_APPLICABLE",
            notes=test.notes,
        )
        for test in selected if selected_profile not in test.hardware_profiles
    ]
    runnable = [test for test in selected if selected_profile in test.hardware_profiles]
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as executor:
        futures = {
            executor.submit(
                run_test, test, workspace, build_dir, bsnes_core, output_root,
                args.clover_only, selected_profile,
            ): test
            for test in runnable
        }
        for future in concurrent.futures.as_completed(futures):
            result = future.result()
            results.append(result)
            detail = ""
            if result.bsnes_compared:
                detail = " bsnes=exact" if result.bsnes_exact else f" bsnes=diff({result.differing_pixels})"
            if result.error:
                detail += f" {result.error}"
            print(f"[{result.status:21}] {result.id}{detail}", flush=True)

    order = {test.id: index for index, test in enumerate(selected)}
    results.sort(key=lambda result: order[result.id])
    write_reports(output_root, manifest, selected_profile, results)

    failures = [result for result in results if result.status in {
        "ERROR", "BSNES_ERROR", "MISSING_REFERENCE", "SELF_REPORT_FAIL", "HARDWARE_DIFFERENCE"
    }]
    unresolved = [result for result in results if result.status not in {"VERIFIED", "NOT_APPLICABLE"}]
    verified_count = sum(result.status == "VERIFIED" for result in results)
    print(f"\nReport: {output_root / 'report.md'}")
    print(f"Machine report: {output_root / 'report.json'}")
    print(
        f"Failures: {len(failures)}; verified: {verified_count}; "
        f"unresolved/evidence-only: {len(unresolved)}"
    )

    failed = bool(failures) or (args.strict and bool(unresolved))
    print(f"Artifacts preserved: {output_root}")
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
