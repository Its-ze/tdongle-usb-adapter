param(
    [string]$Version = "0.2.1",
    [string]$FirmwareDir = ".\firmware\voidlink-ncm-adapter",
    [string]$SiteDir = ".\site"
)

$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$firmwarePath = Join-Path $root $FirmwareDir
$buildDir = Join-Path $firmwarePath "build"
$sitePath = Join-Path $root $SiteDir

if (-not (Get-Command idf.py -ErrorAction SilentlyContinue)) {
    throw "ESP-IDF idf.py is not on PATH. Open an ESP-IDF shell first."
}

Push-Location $firmwarePath
try {
    idf.py set-target esp32s3
    idf.py build
}
finally {
    Pop-Location
}

python (Join-Path $root "tools\package_site.py") --build-dir $buildDir --site-dir $sitePath --version $Version
Write-Host "VoidLink installer packaged at $sitePath"
