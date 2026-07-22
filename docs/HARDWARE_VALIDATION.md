# SNES Hardware Validation Laboratory

Clover's accuracy target is SNES hardware, not agreement with any particular
emulator. The validation system therefore keeps four kinds of evidence separate:

1. **Real-hardware references** — captures with a documented console revision,
   capture path, ROM hash, procedure, and observation frame. An exact match may
   be reported as `VERIFIED`.
2. **Self-reporting conformance ROMs** — ROMs that display `Passed` or `Failed`.
   The runner decodes that terminal result from Clover's framebuffer. These are
   behavioral gates, although the test's assumptions and hardware scope still
   matter.
3. **Characterization ROMs** — experiments whose expected image can vary by
   CPU/PPU revision or analog capture path. Their output is evidence until it is
   compared with a matching, documented hardware profile.
4. **Differential and retail regression tests** — bsnes comparison and gameplay
   sweeps are excellent at exposing change. Agreement is not proof that either
   emulator matches hardware.

Never promote a bsnes frame to a hardware reference. Never collapse a
revision-dependent characterization result into a universal pass/fail claim.

The canonical selection is `late-3chip`, defined by
[`SNES_HARDWARE_MODEL.md`](SNES_HARDWARE_MODEL.md). The manifest's hardware
profile catalog is machine-readable and must agree with the core catalog.

## Test Inventory

[`validation/hardware_tests.json`](../validation/hardware_tests.json) catalogs
the curated runnable payloads from blargg, undisbeliever, gilyon, Peter Lemon,
Sour, and focused community PPU collections. The selection rationale, imported
source revisions, deferred device/enhancement-chip lanes, and verdict hierarchy
are recorded in [`SNES_TEST_CORPUS.md`](SNES_TEST_CORPUS.md).

Each entry has a stable ID, source collection, subsystem category, suite tags,
automation requirement, observation frame, hardware-profile scope, and SHA-256
identity. Archive members are first-class tests and are hash-checked after
extraction. Memory dumps and notes are supporting material, not executable ROMs.

Despite its local directory name, `snes-test-roms` is not an official Nintendo
test collection. Nintendo's known official programs include the SNES Aging Test
Program, Controller Test Program, and SNES Test Program; none is presently in
the local inventory.

## Running the Laboratory

The smoke lane catalogs representative CPU, PPU, DMA/HDMA, and APU cases.
It executes those applicable to the selected profile and reports the remainder
as `NOT_APPLICABLE`:

```bash
python3 scripts/run_hardware_validation.py --suite smoke
```

Run all cataloged tests, or one subsystem:

```bash
python3 scripts/run_hardware_validation.py --suite all
python3 scripts/run_hardware_validation.py --suite apu
python3 scripts/run_hardware_validation.py --suite hdma
python3 scripts/run_hardware_validation.py --scenario blargg-spc-smp
```

The runner defaults to the manifest's canonical profile. An explicit selection
is available for a profile after its implementation is enabled:

```bash
python3 scripts/run_hardware_validation.py --suite all \
  --hardware-profile late-3chip
```

The runner performs CTest preflight unless `--skip-tests` is requested, verifies
every ROM hash, uses deterministic startup entropy, enforces Clover's frame and
terminal-PC guardrails, captures the observation frame, optionally runs bsnes,
and writes both `report.json` and `report.md`. Artifacts are always retained;
the printed directory is the evidence bundle.

Useful controls:

```bash
python3 scripts/run_hardware_validation.py --suite all --clover-only
python3 scripts/run_hardware_validation.py --suite all --jobs 8
python3 scripts/run_hardware_validation.py --suite all --list
python3 scripts/run_hardware_validation.py --suite all \
  --output-dir /private/tmp/clover-hardware-baseline
```

`--strict` requires every applicable selected case to be backed by an approved
exact hardware reference. This is intentionally stronger than the ordinary
baseline and will remain red until the hardware-reference library is populated.

## Status Vocabulary

- `VERIFIED` — exact match to a declared real-hardware reference
- `HARDWARE_DIFFERENCE` — compared with the declared hardware reference and differs
- `SELF_REPORT_PASS` / `SELF_REPORT_FAIL` — decoded terminal ROM result
- `SELF_REPORT_PENDING` — the capture frame did not yet show a terminal result
- `BSNES_MATCH` / `BSNES_DIFFERENCE` — differential evidence only
- `OBSERVED` — Clover observation captured without a comparable reference
- `NOT_APPLICABLE` — the test targets a different physical hardware profile
- `NEEDS_AUTOMATION` — a required manual procedure is not encoded yet
- `ERROR`, `BSNES_ERROR`, `MISSING_REFERENCE` — infrastructure failure

`SELF_REPORT_FAIL` and infrastructure errors fail an ordinary run.
`NOT_APPLICABLE` is excluded from strict unresolved counts. Evidence-only
differences remain visible without pretending that bsnes is authoritative.

## Initial Baseline (2026-07-21)

The first complete 48-payload run passed CTest preflight and completed without
infrastructure errors. It produced:

- 10 decoded self-report passes
- 3 decoded self-report failures: `spc_mem_access_times`, `spc_smp`, and
  `speed_2_freezes2`
- 3 nonterminal self-report observations: `spc_dsp6`, `test_timer_speed3`, and
  `1-test_exec_from_io`
- 25 characterization frames matching bsnes exactly
- 5 characterization differences: both HDMAEN latch tests plus INIDISP
  brightness delay, mid-frame enable, and force-blank cases
- 2 procedures needing deterministic automation: controller strobe behavior
  and timer-at-power/reset

These are triage categories, not 35 hardware passes. In particular, the five
bsnes differences are strong investigation leads and the 25 matches remain
differential evidence until hardware references exist.

Subsequent triage corrected the SPC memory-access ordering defects behind
`spc_mem_access_times` and `spc_smp`. `speed_2_freezes2` was reclassified as
characterization evidence: it exercises unstable high-divider behavior in the
SMP TEST register, and its binary verdict relies on older assumptions that do
not hold consistently across physical consoles. Clover and bsnes produce the
same observation for the canonical deterministic late-S-APU model; changing
the core merely to force this ROM to print `Passed` would be less faithful.

The three initially nonterminal observations were also resolved without hiding
failures. `spc_dsp6` is a long-running suite and reaches `PASSED TESTS` by frame
12000. `1-test_exec_from_io` displays `Passed` at frame 600, then later clears
the result screen. `test_timer_speed3` had already completed at frame 300 with
a numeric measurement table and `Done`, so it is correctly treated as
characterization evidence rather than a binary self-report test.

Both initially unautomated procedures are now deterministic manifest inputs.
The controller test holds B through frame 300 and then releases it; the
power/reset test issues a warm reset before frame 301. Both ROMs self-report
`Passed`, and both observation frames match bsnes exactly. The latter test also
exposed a core defect: `$213C` was returning raw master-clock position instead
of the SNES PPU dot count, including the stretched-dot corrections. Despite the
ROM filename, its measurement is of the PPU H/V counter latch rather than an
SPC timer.

## Expanded Corpus Pass (2026-07-21)

The local laboratory now contains 95 hash-pinned scenarios from six provenance
groups. The expansion added comprehensive gilyon CPU/SPC700 functional ROMs,
30 independent Peter Lemon instruction tests, Sour's CPU/DMA timing pair, and
13 focused community PPU tests.

The first pass immediately found hardware-model defects that retail boot paths
did not isolate. Correcting them made gilyon's complete 65C816 functional ROM
self-report `Success` at terminal test `0649`; its terminal frame is also exact
against bsnes. The fixes cover:

- 24-bit carry between data-bank bytes and indexed data-bank addresses;
- emulation-mode direct-page wrapping, including the `(direct,X)` pointer quirk;
- 16-bit read/modify/write bank carry;
- native-stack internal cycles used by JSL, indexed-indirect JSR, RTL, PEA,
  PEI, PER, PHD, PLD, and PLB while the CPU is in emulation mode;
- decimal ADC/SBC result and flag behavior, including invalid BCD digits.

All 30 Peter Lemon CPU/SPC700 observation frames now match bsnes exactly. The
community PPU lane has eight exact observations. Its five deliberate red leads
are three 512x480-versus-256x240 high-resolution/interlace presentation gaps
(Mode 6, PPU bus activity, and split screen) plus the two HBlank VRAM-DMA
stress cases. Sour's DMA/interrupt table also exposes several one-instruction
boundary differences and remains a timing investigation lead.

These counts are differential triage, not claims of 95 hardware-verified
passes. The real-hardware-reference lane remains the authority.

## Adding Real-Hardware Evidence

Place approved frames below `validation/hardware/references/` using one directory
per test and hardware profile. Add a sidecar metadata file containing at least:

- test ID and exact ROM SHA-256
- console region and board/model identifiers
- S-CPU and PPU revisions when known
- digital or analog capture hardware and processing path
- power-on/reset/input procedure
- exact observation point and any frame-boundary convention
- capture date, operator, and source provenance
- hashes of the raw and normalized artifacts

Normalize only geometry and lossless pixel representation. Color conversion,
cropping, deinterlacing, or analog sampling decisions must be documented and
must not erase the behavior under test. Then set the manifest entry's
`reference` to the approved normalized PPM. A reference is profile-specific;
multiple correct hardware results should become multiple scenarios, not masks.

## Growth Path

The next infrastructure steps are deliberately explicit:

1. add terminal-frame rules for slow self-reporting ROMs rather than guessing a
   convenient frame count;
2. acquire or reproduce provenance-rich real-hardware captures for each relevant
   console revision;
3. add direct machine-state assertions where a ROM exposes a stable result code;
4. make the smoke lane a required local/CI job where legal test ROM availability
   permits it, while keeping local ROM bytes out of source control.

The existing retail accuracy fence remains valuable and independent. Hardware
tests find isolated machine-model defects; retail sweeps prevent those fixes from
regressing real workloads.

## Source Notes

- [SNESdev Emulator Tests](https://snes.nesdev.org/wiki/Emulator_tests) indexes
  both blargg and undisbeliever test material.
- [The SNESdev discussion of blargg's SPC tests](https://forums.nesdev.org/viewtopic.php?start=15&t=18005)
  records that `speed_2_freezes2` targets the SMP TEST register, was based on
  older assumptions, and can produce different lockup behavior across real
  console models.
- [undisbeliever/snes-test-roms](https://github.com/undisbeliever/snes-test-roms)
  is the upstream source for the Marcus Rowe characterization ROMs.
- [gilyon/snes-tests](https://github.com/gilyon/snes-tests) supplies the
  comprehensive 65C816 and SPC700 functional conformance ROMs.
- [PeterLemon/SNES](https://github.com/PeterLemon/SNES) supplies an independent
  instruction-level CPU/SPC700 cross-check and the WAI/STP/reset gap coverage.
- [SourMesen/SnesTests](https://github.com/SourMesen/SnesTests) supplies the
  opcode-cycle and DMA/interrupt-boundary timing experiments.
- [INIDISP Hardware Tests](https://undisbeliever.net/snesdev/registers/inidisp.html)
  documents the observed 1CHIP/3-chip and analog brightness-delay distinctions.
- [TASVideos SNES Accuracy Tests](https://tasvideos.org/EmulatorResources/SNESAccuracyTests)
  distinguishes Nintendo's official test programs from community tests.
