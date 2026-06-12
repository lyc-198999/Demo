[CmdletBinding()]
param(
    [string]$Version = "0.1.0",
    [string]$Configuration = "Release",
    [string]$Platform = "x64",
    [string]$QtBin = "D:\Qt\6.5.3\msvc2019_64\bin",
    [string]$OpenCvBin = "D:\openCV\opencv\build\x64\vc16\bin",
    [string]$SpinnakerBin = "D:\Program Files\Teledyne\Spinnaker\bin64\vs2015",
    [string]$VcRuntimeBin = "$env:WINDIR\System32"
)

$ErrorActionPreference = "Stop"

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$exePath = Join-Path $repoRoot "$Platform\$Configuration\AutoFocusSystem.exe"
if (!(Test-Path -LiteralPath $exePath)) {
    throw "Executable not found: $exePath. Build $Configuration|$Platform first."
}

$packageName = "AutoFocusSystem-v$Version-windows-x64"
$releaseRoot = Join-Path $repoRoot "release"
$packageDir = Join-Path $releaseRoot $packageName
$zipPath = Join-Path $releaseRoot "$packageName.zip"

if (Test-Path -LiteralPath $packageDir) {
    Remove-Item -LiteralPath $packageDir -Recurse -Force
}
if (Test-Path -LiteralPath $zipPath) {
    Remove-Item -LiteralPath $zipPath -Force
}

New-Item -ItemType Directory -Force -Path $packageDir | Out-Null
Copy-Item -LiteralPath $exePath -Destination (Join-Path $packageDir "AutoFocusSystem.exe")

$openCvDlls = @(
    "opencv_world4120.dll",
    "opencv_videoio_ffmpeg4120_64.dll"
)
foreach ($dll in $openCvDlls) {
    $source = Join-Path $OpenCvBin $dll
    if (Test-Path -LiteralPath $source) {
        Copy-Item -LiteralPath $source -Destination $packageDir -Force
    }
}

$spinnakerDlls = @(
    "Spinnaker_v140.dll",
    "GCBase_MD_VC140_v3_0.dll",
    "GenApi_MD_VC140_v3_0.dll",
    "Log_MD_VC140_v3_0.dll",
    "MathParser_MD_VC140_v3_0.dll",
    "NodeMapData_MD_VC140_v3_0.dll",
    "XmlParser_MD_VC140_v3_0.dll",
    "libiomp5md.dll"
)
foreach ($dll in $spinnakerDlls) {
    $source = Join-Path $SpinnakerBin $dll
    if (Test-Path -LiteralPath $source) {
        Copy-Item -LiteralPath $source -Destination $packageDir -Force
    }
}

$winDeployQt = Join-Path $QtBin "windeployqt.exe"
if (!(Test-Path -LiteralPath $winDeployQt)) {
    throw "windeployqt not found: $winDeployQt"
}

& $winDeployQt --release --compiler-runtime (Join-Path $packageDir "AutoFocusSystem.exe")
if ($LASTEXITCODE -ne 0) {
    throw "windeployqt failed with exit code $LASTEXITCODE"
}

$vcRuntimeDlls = @(
    "concrt140.dll",
    "msvcp140.dll",
    "msvcp140_1.dll",
    "msvcp140_2.dll",
    "msvcp140_atomic_wait.dll",
    "msvcp140_codecvt_ids.dll",
    "vccorlib140.dll",
    "vcomp140.dll",
    "vcruntime140.dll",
    "vcruntime140_1.dll",
    "vcruntime140_threads.dll"
)
foreach ($dll in $vcRuntimeDlls) {
    $source = Join-Path $VcRuntimeBin $dll
    if (Test-Path -LiteralPath $source) {
        Copy-Item -LiteralPath $source -Destination $packageDir -Force
    }
}

$readme = @"
AutoFocusSystem $Version

1. Connect the FLIR camera and motor controller before launching the application.
2. Run AutoFocusSystem.exe.
3. Select the serial port and baud rate in the Connection tab.
4. Use Manual mode for coarse positioning, then switch to Auto mode for autofocus.

Notes:
- The target computer must have the proper camera driver installed.
- Image and log exports are saved to the configured data directory in the application.
"@
Set-Content -LiteralPath (Join-Path $packageDir "README.txt") -Value $readme -Encoding UTF8

Compress-Archive -LiteralPath $packageDir -DestinationPath $zipPath -Force
Write-Host "Created package: $zipPath"
