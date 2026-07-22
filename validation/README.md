# Accuracy Fence Scenarios

`accuracy_fence.json` is the checked-in contract consumed by
`scripts/run_accuracy_fence.py`. It identifies local ROMs by SHA-256, defines
exact frame ranges, and connects input-gated paths to deterministic controller
movies without committing commercial ROM data.

## Suites

- `baseline` — every power-on frame through frame 2000 for the five milestone ROMs
- `interactive` — input-gated ranges already proven exact against bsnes
- `investigation` — reproducible paths with an unresolved divergence; these
  are expected to fail until the underlying issue is closed
- `full` — runner-only selection that combines `baseline` and `interactive`

The default command runs `full`. Investigation scenarios are never silently
included in a green result.

## Scenario Fields

Required fields:

- `id` — unique command-line identifier
- `suite` — `baseline`, `interactive`, or `investigation`
- `rom` and `sha256` — local path and exact required ROM identity
- `start_frame` and `end_frame` — inclusive exact comparison range

Optional fields:

- `input_script` — frame-indexed SNES serial joypad state
- `save_ram` and `save_ram_sha256` — optional immutable initial battery RAM and
  its required identity; both Clover and bsnes receive private/read-only inputs
- `bsnes_input_frame_offset` — shifts the input movie for the bsnes runner to
  align its libretro presented-frame observation boundary with Clover's
  pre-`run_frame()` input boundary
- `step_limit` — per-scenario Clover hardware-step guardrail
- `compare_profile` — currently required to remain `exact`

Video comparison always uses frame offset zero. Input-boundary alignment must
never become permission to search for or accept a visual offset.

## Adding a Scenario

1. Record the path with the SDL version-4 capture workflow.
2. Copy `joypad1.script` and, when present, `initial_save_ram.srm` into
   `validation/input/`.
3. Add the ROM SHA-256 and the smallest frame range that fully covers the
   hardware behavior under test.
4. Run the scenario explicitly and require exact Clover-versus-bsnes output.
5. Place it in `interactive` only after it passes. Otherwise keep it visible in
   `investigation` with the discrepancy documented.

Passing temporary images are deleted automatically. A failure preserves its
logs and frames under the printed `/private/tmp/clover-accuracy-fence-*` path.

## Hardware Validation

The separate [`hardware_tests.json`](hardware_tests.json) manifest catalogs
self-reporting conformance and hardware-characterization ROMs. Run its smoke
lane with:

```bash
python3 scripts/run_hardware_validation.py --suite smoke
```

The manifest declares `late-3chip` as Clover's canonical profile. Every test is
explicitly scoped to one or more physical profiles; revision-only cases are
reported as `NOT_APPLICABLE` when another profile is selected.

Unlike the retail accuracy fence, this runner does not treat bsnes as an oracle.
It distinguishes decoded ROM results, real-hardware references, revision-scoped
characterization, and differential evidence. See
[`docs/HARDWARE_VALIDATION.md`](../docs/HARDWARE_VALIDATION.md).
The expanded corpus and its inclusion/deferment rules are documented in
[`docs/SNES_TEST_CORPUS.md`](../docs/SNES_TEST_CORPUS.md).
