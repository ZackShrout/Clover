[CmdletBinding()]
param(
    [Parameter(Mandatory = $true, Position = 0)]
    [string]$RomPath,

    [ValidateRange(600, 36000)]
    [int]$Frames = 7200,

    [string]$OutputRoot = "",

    [string]$PerfViewPath = "",

    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

function Write-ManifestValue {
    param(
        [Parameter(Mandatory = $true)]
        [System.IO.StreamWriter]$Writer,
        [Parameter(Mandatory = $true)]
        [string]$Name,
        [AllowEmptyString()]
        [string]$Value
    )

    $singleLine = $Value -replace "`r?`n", " | "
    $Writer.WriteLine("{0}={1}", $Name, $singleLine)
}

function Resolve-DiagnosticLog {
    $candidates = @(
        (Join-Path $env:APPDATA "BunnySoft\Clover\logs\clover-latest.log"),
        (Join-Path $env:LOCALAPPDATA "BunnySoft\Clover\logs\clover-latest.log")
    )
    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            return $candidate
        }
    }
    return $null
}

function Resolve-PerformanceTimeline {
    $candidates = @(
        (Join-Path $env:APPDATA "BunnySoft\Clover\logs\clover-performance.csv"),
        (Join-Path $env:LOCALAPPDATA "BunnySoft\Clover\logs\clover-performance.csv")
    )
    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            return $candidate
        }
    }
    return $null
}

function Initialize-VisualStudioToolchain {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} `
        "Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path -LiteralPath $vswhere -PathType Leaf)) {
        throw "Visual Studio Installer's vswhere.exe was not found at $vswhere."
    }

    $installationPath = (& $vswhere -latest -products * `
        -version "[17.0,18.0)" `
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
                  Microsoft.VisualStudio.Component.VC.Llvm.Clang `
        -property installationPath | Select-Object -First 1)
    if ([string]::IsNullOrWhiteSpace($installationPath)) {
        throw "Visual Studio 2022 with MSVC and bundled clang-cl was not found."
    }
    $installationPath = $installationPath.Trim()

    $developerCommand = Join-Path $installationPath "Common7\Tools\VsDevCmd.bat"
    if (-not (Test-Path -LiteralPath $developerCommand -PathType Leaf)) {
        throw "Visual Studio's VsDevCmd.bat was not found at $developerCommand."
    }

    # A Developer PowerShell can retain an older default toolset even after
    # 14.44 is installed. Import the exact environment used by release CI into
    # this process rather than relying on the shell's initial selection.
    $environmentLines = & $env:COMSPEC /s /c `
        "`"$developerCommand`" -no_logo -arch=x64 -host_arch=x64 -vcvars_ver=14.44 && set"
    if ($LASTEXITCODE -ne 0) {
        throw "Visual Studio could not initialize MSVC toolset 14.44."
    }
    foreach ($line in $environmentLines) {
        if ($line -match "^([^=]+)=(.*)$") {
            [Environment]::SetEnvironmentVariable(
                $matches[1],
                $matches[2],
                [EnvironmentVariableTarget]::Process
            )
        }
    }

    if ($env:VCToolsVersion -notlike "14.44.*") {
        throw "MSVC toolset 14.44 is required; Visual Studio initialized $env:VCToolsVersion. Confirm that 'MSVC v143 - VS 2022 C++ x64/x86 build tools (v14.44-17.14)' is installed."
    }

    $clangCl = Join-Path $installationPath `
        "VC\Tools\Llvm\x64\bin\clang-cl.exe"
    if (-not (Test-Path -LiteralPath $clangCl -PathType Leaf)) {
        throw "Visual Studio's bundled clang-cl was not found at $clangCl."
    }

    $banner = (& $clangCl --version 2>&1) -join "`n"
    if ($LASTEXITCODE -ne 0 -or $banner -notmatch "clang version 19\.1\.5(?:\s|$)") {
        throw "Visual Studio's bundled clang-cl 19.1.5 is required.`n$banner"
    }
    return $clangCl
}

$scriptRoot = Split-Path -Parent $PSCommandPath
$repoRoot = Split-Path -Parent $scriptRoot
$resolvedRom = (Resolve-Path -LiteralPath $RomPath).Path
$clangCl = Initialize-VisualStudioToolchain

Write-Host "Using compiler: $clangCl"
Write-Host ((& $clangCl --version 2>&1 | Select-Object -First 1) -join "")
Write-Host "Using MSVC tools: $env:VCToolsVersion"

if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
    $desktop = [Environment]::GetFolderPath([Environment+SpecialFolder]::Desktop)
    $OutputRoot = Join-Path $desktop "CloverProfiles"
}
$OutputRoot = [System.IO.Path]::GetFullPath($OutputRoot)
$timestamp = Get-Date -Format "yyyyMMdd-HHmmss"
$outputDirectory = Join-Path $OutputRoot "clover-profile-$timestamp"
New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null

$beforeLog = Resolve-DiagnosticLog
if ($null -ne $beforeLog) {
    Copy-Item -LiteralPath $beforeLog `
        -Destination (Join-Path $outputDirectory "before-profile-clover-latest.log")
}
$beforeTimeline = Resolve-PerformanceTimeline
if ($null -ne $beforeTimeline) {
    Copy-Item -LiteralPath $beforeTimeline `
        -Destination (Join-Path $outputDirectory "before-profile-clover-performance.csv")
}

if (-not $SkipBuild) {
    Write-Host "Configuring the optimized clang-cl profiling build..."
    Push-Location $repoRoot
    try {
        # --fresh discards any compiler path cached by a previous failed
        # configure. The explicit path prevents a system LLVM installation
        # earlier on PATH from replacing Visual Studio's pinned clang-cl.
        & cmake --fresh --preset windows-clangcl-profile `
            "-DCMAKE_CXX_COMPILER=$clangCl"
        if ($LASTEXITCODE -ne 0) {
            throw "CMake configure failed with exit code $LASTEXITCODE."
        }

        Write-Host "Building Clover with native profiling symbols..."
        & cmake --build --preset windows-clangcl-profile
        if ($LASTEXITCODE -ne 0) {
            throw "CMake build failed with exit code $LASTEXITCODE."
        }
    }
    finally {
        Pop-Location
    }
}

$buildDirectory = Join-Path $repoRoot "cmake-build-windows-clangcl-profile\Release"
$cloverExe = Join-Path $buildDirectory "Clover.exe"
$cloverPdb = Join-Path $buildDirectory "Clover.pdb"
if (-not (Test-Path -LiteralPath $cloverExe -PathType Leaf)) {
    throw "Profiling executable not found at $cloverExe."
}
if (-not (Test-Path -LiteralPath $cloverPdb -PathType Leaf)) {
    throw "Profiling symbols not found at $cloverPdb."
}

if ([string]::IsNullOrWhiteSpace($PerfViewPath)) {
    $toolDirectory = Join-Path $env:LOCALAPPDATA "CloverProfiling"
    $PerfViewPath = Join-Path $toolDirectory "PerfView.exe"
    if (-not (Test-Path -LiteralPath $PerfViewPath -PathType Leaf)) {
        New-Item -ItemType Directory -Path $toolDirectory -Force | Out-Null
        Write-Host "Downloading Microsoft's signed PerfView executable..."
        Invoke-WebRequest -UseBasicParsing `
            -Uri "https://aka.ms/perfview/latest" `
            -OutFile $PerfViewPath
    }
}
$PerfViewPath = (Resolve-Path -LiteralPath $PerfViewPath).Path

$signature = Get-AuthenticodeSignature -LiteralPath $PerfViewPath
if ($signature.Status -ne [System.Management.Automation.SignatureStatus]::Valid `
    -or $null -eq $signature.SignerCertificate `
    -or $signature.SignerCertificate.Subject -notmatch "Microsoft Corporation") {
    throw "PerfView did not have a valid Microsoft Corporation Authenticode signature."
}

$tracePath = Join-Path $outputDirectory "clover-cpu.etl"
$perfViewLog = Join-Path $outputDirectory "perfview-collection.log"
$perfViewArguments = @(
    "/AcceptEULA",
    "/NoGui",
    "/NoView",
    "/NoClrRundown",
    "/NoNGenRundown",
    "/ClrEvents:None",
    "/KernelEvents:Default",
    "/CpuSampleMSec:1",
    "/Zip:true",
    "/DataFile:$tracePath",
    "/LogFile:$perfViewLog",
    "run",
    $cloverExe,
    $resolvedRom,
    "--frames",
    $Frames.ToString([Globalization.CultureInfo]::InvariantCulture)
)

Write-Host ""
Write-Host "Starting the profile. Windows may request administrator permission."
Write-Host "Play the same representative section until Clover exits automatically."
Write-Host "Press F8 once immediately before triggering the section you want to measure."
Write-Host "Requested frame count: $Frames (about $([math]::Round($Frames / 3600.0, 1)) minutes at full speed)."
Write-Host ""

Push-Location $buildDirectory
try {
    & $PerfViewPath @perfViewArguments
    if ($LASTEXITCODE -ne 0) {
        throw "PerfView failed with exit code $LASTEXITCODE. See $perfViewLog."
    }
}
finally {
    Pop-Location
}

$traceArchive = "$tracePath.zip"
$waitSeconds = [Math]::Max(
    300,
    [Math]::Ceiling($Frames / 20.0) + 180
)
$collectionDeadline = [DateTime]::UtcNow.AddSeconds($waitSeconds)
$reportedBackgroundCollection = $false
while (-not (Test-Path -LiteralPath $traceArchive -PathType Leaf)) {
    if (-not $reportedBackgroundCollection) {
        Write-Host "PerfView elevated its collector; waiting for the trace archive..."
        $reportedBackgroundCollection = $true
    }
    if ([DateTime]::UtcNow -ge $collectionDeadline) {
        $directoryContents = (
            Get-ChildItem -LiteralPath $outputDirectory -ErrorAction SilentlyContinue |
                Select-Object -ExpandProperty Name
        ) -join ", "
        $logTail = ""
        if (Test-Path -LiteralPath $perfViewLog -PathType Leaf) {
            $logTail = (
                Get-Content -LiteralPath $perfViewLog -Tail 30 |
                    Out-String
            ).Trim()
        }
        throw "Timed out after $waitSeconds seconds waiting for $traceArchive. Files present: $directoryContents`nPerfView log tail:`n$logTail"
    }
    Start-Sleep -Seconds 1
}

Copy-Item -LiteralPath $cloverExe -Destination $outputDirectory
Copy-Item -LiteralPath $cloverPdb -Destination $outputDirectory

$afterLog = Resolve-DiagnosticLog
if ($null -ne $afterLog) {
    Copy-Item -LiteralPath $afterLog `
        -Destination (Join-Path $outputDirectory "profile-clover-latest.log")
}
$afterTimeline = Resolve-PerformanceTimeline
if ($null -ne $afterTimeline) {
    Copy-Item -LiteralPath $afterTimeline `
        -Destination (Join-Path $outputDirectory "profile-clover-performance.csv")
}

$manifestPath = Join-Path $outputDirectory "profile-manifest.txt"
$manifest = [System.IO.StreamWriter]::new($manifestPath, $false, [Text.Encoding]::UTF8)
try {
    Write-ManifestValue $manifest "profile_schema" "2"
    Write-ManifestValue $manifest "collected_at" (Get-Date -Format "o")
    Write-ManifestValue $manifest "frames_requested" $Frames.ToString()
    Write-ManifestValue $manifest "rom_filename" ([System.IO.Path]::GetFileName($resolvedRom))
    Write-ManifestValue $manifest "rom_sha256" (Get-FileHash -LiteralPath $resolvedRom -Algorithm SHA256).Hash
    Write-ManifestValue $manifest "clover_exe_sha256" (Get-FileHash -LiteralPath $cloverExe -Algorithm SHA256).Hash
    Write-ManifestValue $manifest "clover_pdb_sha256" (Get-FileHash -LiteralPath $cloverPdb -Algorithm SHA256).Hash
    Write-ManifestValue $manifest "perfview_sha256" (Get-FileHash -LiteralPath $PerfViewPath -Algorithm SHA256).Hash
    Write-ManifestValue $manifest "perfview_signer" $signature.SignerCertificate.Subject

    $revision = (& git -C $repoRoot rev-parse HEAD 2>&1 | Out-String).Trim()
    Write-ManifestValue $manifest "git_revision" $revision
    Write-ManifestValue $manifest "git_dirty" ((& git -C $repoRoot status --porcelain | Measure-Object).Count -ne 0).ToString()

    $os = Get-CimInstance Win32_OperatingSystem
    $cpu = Get-CimInstance Win32_Processor | Select-Object -First 1
    $computer = Get-CimInstance Win32_ComputerSystem
    Write-ManifestValue $manifest "os" "$($os.Caption) $($os.Version) build $($os.BuildNumber)"
    Write-ManifestValue $manifest "cpu" $cpu.Name.Trim()
    Write-ManifestValue $manifest "cpu_physical_cores" $cpu.NumberOfCores.ToString()
    Write-ManifestValue $manifest "cpu_logical_processors" $cpu.NumberOfLogicalProcessors.ToString()
    Write-ManifestValue $manifest "memory_bytes" $computer.TotalPhysicalMemory.ToString()
    Write-ManifestValue $manifest "power_plan" ((& powercfg /getactivescheme 2>&1 | Out-String).Trim())

    $battery = Get-CimInstance Win32_Battery -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($null -ne $battery) {
        Write-ManifestValue $manifest "battery_status" $battery.BatteryStatus.ToString()
        Write-ManifestValue $manifest "battery_charge_percent" $battery.EstimatedChargeRemaining.ToString()
    } else {
        Write-ManifestValue $manifest "battery_status" "not_reported"
        Write-ManifestValue $manifest "battery_charge_percent" "not_reported"
    }

    Write-ManifestValue $manifest "clang_cl_path" $clangCl
    Write-ManifestValue $manifest "clang_cl" ((& $clangCl --version 2>&1 | Select-Object -First 1 | Out-String).Trim())
    Write-ManifestValue $manifest "cmake" ((& cmake --version 2>&1 | Select-Object -First 1 | Out-String).Trim())
}
finally {
    $manifest.Dispose()
}

$bundlePath = "$outputDirectory.zip"
Compress-Archive -LiteralPath $outputDirectory -DestinationPath $bundlePath -CompressionLevel Optimal

Write-Host ""
Write-Host "Profile complete."
Write-Host "Send this bundle back for analysis:"
Write-Host $bundlePath
