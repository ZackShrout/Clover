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
speed. Load the same save and play the representative slow section. Clover
exits automatically at the frame limit.

The first run:

1. locates and verifies Visual Studio's bundled clang-cl 19.1.5, then performs
   a fresh `windows-clangcl-profile` configure so another LLVM installation
   cannot be retained by CMake;
2. builds the profile target with Release optimization and native PDB
   information;
3. downloads the current official Microsoft PerfView executable when it is not
   already cached;
4. verifies its Microsoft Authenticode signature;
5. requests administrator permission required by ETW;
6. records 1 ms CPU samples and merged native call stacks;
7. writes a timestamped ZIP under `Desktop\CloverProfiles`.

The ZIP contains the merged `clover-cpu.etl.zip` trace, its exact `Clover.exe`
and `Clover.pdb`, PerfView's collection log, Clover's before/after diagnostics,
and a manifest containing build, CPU, OS, power, and file-identity information.
Send that outer ZIP for analysis. ROM bytes and save data are not included.

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
