# Clover third-party notices

Clover is distributed without games, ROM images, firmware, or Nintendo
assets. Users must supply media they are legally entitled to use.

## snes_spc / SPC_DSP

Clover includes a modified version of `snes_spc` 0.9.0's `SPC_DSP` module:

- Copyright © 2007 Shay Green
- Licensed under the GNU Lesser General Public License, version 2.1 or,
  at your option, any later version.
- The complete license is in `LICENSES/LGPL-2.1.txt`.
- The corresponding modified source is under
  `src/clover/core/snes/dsp/` in the Clover source tree.

Every GitHub release is associated with a Git tag. The complete Clover source
and build scripts for that exact tag are available from the release page so
recipients can modify the LGPL-covered module and rebuild/relink Clover.

## Snes9x-derived DSP material

Clover's DSP-1 reference data and portions of its DSP-4 implementation are
derived from work by the Snes9x DSP contributors identified in the source
headers and in `LICENSES/Snes9x.txt`.

Permission is granted by the upstream license only for non-commercial use,
copying, modification, and distribution with the license information and
copyright notice retained. Clover is distributed free of charge and is not
licensed for commercial redistribution.

## SDL 3

Clover uses Simple DirectMedia Layer 3:

- Copyright © 1997-2026 Sam Lantinga
- Licensed under the zlib license in `LICENSES/SDL3.txt`.

## SQLite

Clover uses SQLite. The SQLite project states that all code authors have
dedicated their contributions to the public domain. See
<https://www.sqlite.org/copyright.html>.

## Trademarks

Super Nintendo Entertainment System and related names are trademarks of
Nintendo. Clover is an independent project and is not affiliated with or
endorsed by Nintendo.
