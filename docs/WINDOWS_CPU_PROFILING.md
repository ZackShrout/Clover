# Windows CPU profiling

This workflow records a native sampling profile of the optimized clang-cl
build. It is intended to answer where Clover's emulation thread spends its
time without adding per-instruction timers to the core.

## Before collecting

- Use the same Windows machine, ROM revision, save, and gameplay section for
  every comparison.
- Connect AC power and record profiles in the Windows power mode that matters
  for the test.
- Close browsers, game launchers, update tools, and other avoidable background
  work.
- Run the released Beta 6 once first if a release-to-profile comparison is
  needed. The script preserves the existing `clover-latest.log` as
  `before-profile-clover-latest.log` before starting the profiling build.

## Collect the profile

Open **Developer PowerShell for VS 2022**, change to the Clover repository, and
run:

```powershell
.\scripts\profile_windows.ps1 -RomPath "C:\path\to\Final Fantasy III.sfc"
```

The default run lasts 7,200 emulated frames, approximately two minutes at full
speed. Load the same save and play the representative slow section. Press F8
once immediately before triggering the section under test (for example, just
before initiating combat). Clover records that frame in its five-second
performance timeline and exits automatically at the frame limit.

The first run:

1. locates Visual Studio, initializes its MSVC 14.44 environment even when the
   shell defaulted to an older installed toolset, and verifies Visual Studio's
   bundled clang-cl 19.1.5;
2. performs a fresh `windows-clangcl-profile` configure so another LLVM
   installation cannot be retained by CMake;
3. builds the profile target with Release optimization and native PDB
   information;
4. downloads the current official Microsoft PerfView executable when it is not
   already cached;
5. verifies its Microsoft Authenticode signature;
6. requests administrator permission required by ETW;
7. waits for the elevated PerfView collector while it records 1 ms CPU samples
   and merges the native call stacks;
8. writes a timestamped ZIP under `Desktop\CloverProfiles`.

The ZIP contains the merged `clover-cpu.etl.zip` trace, its exact `Clover.exe`
and `Clover.pdb`, PerfView's collection log, Clover's before/after diagnostics,
the five-second `clover-performance.csv` timeline with any F8 marker, and a
manifest containing build, CPU, OS, power, and file-identity information. Send
that outer ZIP for analysis. ROM bytes and save data are not included.

Use a different duration when necessary:

```powershell
.\scripts\profile_windows.ps1 `
  -RomPath "C:\path\to\Final Fantasy III.sfc" `
  -Frames 10800
```

After the first successful build, `-SkipBuild` avoids rebuilding:

```powershell
.\scripts\profile_windows.ps1 `
  -RomPath "C:\path\to\Final Fantasy III.sfc" `
  -SkipBuild
```

If PerfView is already installed elsewhere, pass its executable explicitly
with `-PerfViewPath`.
