# Stages Glacier Ref with its Qt and MSVC runtime and writes the ZIP.
#
#   packaging/windows/stage.ps1 -BuildDir <build> -Version <v> -OutDir <dir>
#
# Run in an x64 MSVC environment (VCToolsRedistDir set) with the Qt bin
# directory (windeployqt) on PATH. Leaves the staged folder in
# <OutDir>/stage/<name> for the smoke test; the ZIP holds exactly that folder.
# The portable-mode marker comes with M5.04: until then the ZIP version keeps
# its settings in the user profile like an installed one.
param(
    [Parameter(Mandatory)] [string] $BuildDir,
    [Parameter(Mandatory)] [string] $Version,
    [Parameter(Mandatory)] [string] $OutDir
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$name = "glacier-ref-$Version-windows-x64"
$OutDir = New-Item -ItemType Directory -Force -Path $OutDir | Select-Object -ExpandProperty FullName
$stage = Join-Path $OutDir "stage\$name"
if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }

cmake --install $BuildDir --config Release --prefix $stage
if ($LASTEXITCODE -ne 0) { throw 'cmake --install failed' }
$exe = Join-Path $stage 'glacier-ref.exe'

# Widgets render in software, so neither Mesa nor the D3D shader compiler is
# needed. The MSVC runtime is copied app-locally below instead of shipping
# vc_redist (no installer step for testers).
windeployqt --release --no-translations --no-compiler-runtime --no-opengl-sw --no-system-d3d-compiler $exe
if ($LASTEXITCODE -ne 0) { throw 'windeployqt failed' }

if (-not $env:VCToolsRedistDir) { throw 'VCToolsRedistDir is not set (run in an MSVC x64 environment)' }
$crt = Get-ChildItem -Directory (Join-Path $env:VCToolsRedistDir 'x64') -Filter 'Microsoft.VC*.CRT' | Select-Object -First 1
if (-not $crt) { throw "no MSVC CRT redistributable under $env:VCToolsRedistDir" }
Copy-Item (Join-Path $crt.FullName '*.dll') $stage

$root = Resolve-Path (Join-Path $PSScriptRoot '..\..')
Copy-Item (Join-Path $root 'LICENSE') (Join-Path $stage 'LICENSE.txt')
Copy-Item (Join-Path $root 'THIRD_PARTY_NOTICES.md') $stage

$required = @(
    'glacier-ref.exe', 'Qt6Core.dll', 'Qt6Gui.dll', 'Qt6Widgets.dll', 'Qt6Svg.dll', 'Qt6Concurrent.dll',
    'platforms\qwindows.dll',
    'imageformats\qjpeg.dll', 'imageformats\qgif.dll', 'imageformats\qwebp.dll', 'imageformats\qtiff.dll',
    'msvcp140.dll', 'vcruntime140.dll', 'vcruntime140_1.dll'
)
foreach ($file in $required) {
    if (-not (Test-Path (Join-Path $stage $file))) { throw "missing from the staged folder: $file" }
}

$zip = Join-Path $OutDir "$name.zip"
if (Test-Path $zip) { Remove-Item -Force $zip }
Push-Location (Join-Path $OutDir 'stage')
try {
    7z a -tzip -mx=9 $zip $name | Out-Null
    if ($LASTEXITCODE -ne 0) { throw '7z failed' }
} finally {
    Pop-Location
}
Write-Host "stage: wrote $zip"
