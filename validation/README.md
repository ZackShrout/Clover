# Accuracy Fence Scenarios

`accuracy_fence.json` is the checked-in contract consumed by
`scripts/run_accuracy_fence.py`. It identifies local ROMs by SHA-256, defines
exact frame ranges, and connects input-gated paths to deterministic controller
movies without committing commercial ROM data.

## Suites

- `baseline` — every power-on frame through frame 1000 for the four milestone ROMs
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
- `bsnes_input_frame_offset` — shifts the input movie for the bsnes runner to
  align its libretro presented-frame observation boundary with Clover's
  pre-`run_frame()` input boundary
- `step_limit` — per-scenario Clover hardware-step guardrail
- `compare_profile` — currently required to remain `exact`

Video comparison always uses frame offset zero. Input-boundary alignment must
never become permission to search for or accept a visual offset.

## Adding a Scenario

1. Record the path with the SDL version-3 capture workflow.
2. Copy only `joypad1.script` into `validation/input/`.
3. Add the ROM SHA-256 and the smallest frame range that fully covers the
   hardware behavior under test.
4. Run the scenario explicitly and require exact Clover-versus-bsnes output.
5. Place it in `interactive` only after it passes. Otherwise keep it visible in
   `investigation` with the discrepancy documented.

Passing temporary images are deleted automatically. A failure preserves its
logs and frames under the printed `/private/tmp/clover-accuracy-fence-*` path.
