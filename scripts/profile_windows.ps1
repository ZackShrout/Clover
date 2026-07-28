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

$scriptRoot = Split-Path -Parent $PSCommandPath
$repoRoot = Split-Path -Parent $scriptRoot
$resolvedRom = (Resolve-Path -LiteralPath $RomPath).Path

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

if (-not $SkipBuild) {
    Write-Host "Configuring the optimized clang-cl profiling build..."
    Push-Location $repoRoot
    try {
        & cmake --preset windows-clangcl-profile
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
Write-Host "Requested frame count: $Frames (about $([math]::Round($Frames / 60.0, 1)) minutes at full speed)."
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
if (-not (Test-Path -LiteralPath $traceArchive -PathType Leaf)) {
    throw "PerfView completed without creating $traceArchive."
}

Copy-Item -LiteralPath $cloverExe -Destination $outputDirectory
Copy-Item -LiteralPath $cloverPdb -Destination $outputDirectory

$afterLog = Resolve-DiagnosticLog
if ($null -ne $afterLog) {
    Copy-Item -LiteralPath $afterLog `
        -Destination (Join-Path $outputDirectory "profile-clover-latest.log")
}

$manifestPath = Join-Path $outputDirectory "profile-manifest.txt"
$manifest = [System.IO.StreamWriter]::new($manifestPath, $false, [Text.Encoding]::UTF8)
try {
    Write-ManifestValue $manifest "profile_schema" "1"
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

    Write-ManifestValue $manifest "clang_cl" ((& clang-cl --version 2>&1 | Select-Object -First 1 | Out-String).Trim())
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
