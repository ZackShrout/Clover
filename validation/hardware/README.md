# Hardware Reference Store

This directory defines the shape of Clover's real-SNES evidence library. ROMs
and captures are not assumed redistributable and are not added merely because
they came from another emulator.

Approved references belong under:

```text
references/<test-id>/<hardware-profile>/
  frame_<number>.ppm
  metadata.json
```

The metadata requirements and promotion rules are documented in
[`docs/HARDWARE_VALIDATION.md`](../../docs/HARDWARE_VALIDATION.md). Until a
manifest entry names an approved reference file, characterization results remain
`OBSERVED`, `BSNES_MATCH`, or `BSNES_DIFFERENCE`; they are not hardware-verified.

Non-frame evidence can have a tracked identity sidecar here while its bulky or
redistributability-unclear payload remains under ignored `roms/local/`. The
`ppubusactivity_rev2.json` sidecar pins the raw Sigrok capture and cooked CSV
used by the PPU bus-activity investigation.
