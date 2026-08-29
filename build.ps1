<#
.SYNOPSIS
    Build the self-contained typewriter.exe with the MSYS2 MinGW-w64 toolchain.

.DESCRIPTION
    Wraps the documented build:

        mingw32-make STATIC=1        (SDL2, SDL2_ttf, FreeType linked in)

    The Makefile's pkg-config probing needs a POSIX shell, so the build runs
    inside the MSYS2 MINGW64 environment (usr\bin\bash.exe -lc ...). The MSYS2
    root is auto-detected, so this works from a vanilla PowerShell session.
    The result is a single exe with no DLL dependencies, which is what
    forge.toml packages into the installer.

.PARAMETER Dynamic
    Plain `mingw32-make` instead of STATIC=1: a small exe that needs SDL2.dll
    and SDL2_ttf.dll next to it (development builds).

.PARAMETER Clean
    Run `mingw32-make clean` first (forces a full rebuild).

.PARAMETER Run
    Launch typewriter.exe after a successful build.

.PARAMETER Installer
    After building, also build the installer by invoking build-installer.ps1.

.PARAMETER MsysRoot
    Path to the MSYS2 installation (the folder containing usr\bin\bash.exe).
    Auto-detected when omitted.

.EXAMPLE
    .\build.ps1
    Self-contained Release build -> .\typewriter.exe

.EXAMPLE
    .\build.ps1 -Clean -Run
    Fresh build, then launch the app.

.EXAMPLE
    .\build.ps1 -Installer
    Build the app, then produce dist\Typewriter-Setup-<version>.exe.
#>

[CmdletBinding()]
param(
    [switch]$Dynamic,
    [switch]$Clean,
    [switch]$Run,
    [switch]$Installer,
    [string]$MsysRoot
)

$ErrorActionPreference = "Stop"

$root = $PSScriptRoot

# --- locate MSYS2 --------------------------------------------------------
if (-not $MsysRoot) {
    # Prefer the MSYS2 that owns a gcc already on PATH (...\msys64\mingw64\bin);
    # otherwise probe the usual install spots.
    $gcc = Get-Command gcc -ErrorAction SilentlyContinue
    if ($gcc) {
        $candidate = Split-Path (Split-Path (Split-Path $gcc.Source))
        if (Test-Path (Join-Path $candidate "usr\bin\bash.exe")) { $MsysRoot = $candidate }
    }
    if (-not $MsysRoot) {
        $MsysRoot = @(
            "D:\Apps\msys64",
            "C:\msys64",
            "C:\tools\msys64"
        ) | Where-Object { Test-Path (Join-Path $_ "usr\bin\bash.exe") } | Select-Object -First 1
    }
}

$bash = if ($MsysRoot) { Join-Path $MsysRoot "usr\bin\bash.exe" } else { $null }
if (-not $bash -or -not (Test-Path $bash)) {
    throw "MSYS2 not found. Pass -MsysRoot <path to msys64>."
}

# D:\Workhammer\typewriter -> /d/Workhammer/typewriter for the MSYS2 shell
$posixRoot = "/" + $root.Substring(0, 1).ToLower() + $root.Substring(2).Replace("\", "/")

Write-Host "MSYS2     : $MsysRoot"                              -ForegroundColor Cyan
Write-Host "Mode      : $(if ($Dynamic) { 'dynamic' } else { 'static (self-contained)' })" -ForegroundColor Cyan
Write-Host "Project   : $root"                                  -ForegroundColor Cyan

function Invoke-Msys([string]$command) {
    $env:MSYSTEM       = "MINGW64"
    $env:CHERE_INVOKING = "1"
    & $bash -lc "cd '$posixRoot' && $command"
    if ($LASTEXITCODE -ne 0) { throw "'$command' failed ($LASTEXITCODE)" }
}

# --- clean ---------------------------------------------------------------
if ($Clean) {
    Write-Host "Cleaning ..." -ForegroundColor Yellow
    Invoke-Msys "mingw32-make clean"
}

# --- build ---------------------------------------------------------------
Write-Host "Building (mingw32-make) ..." -ForegroundColor Green
if ($Dynamic) { Invoke-Msys "mingw32-make" } else { Invoke-Msys "mingw32-make STATIC=1" }

$exe = Join-Path $root "typewriter.exe"
if (-not (Test-Path $exe)) { throw "build reported success but $exe is missing." }
Write-Host "Built: $exe ($([math]::Round((Get-Item $exe).Length / 1MB, 1)) MB)" -ForegroundColor Green

# --- optional: installer -------------------------------------------------
if ($Installer) {
    if ($Dynamic) { throw "-Installer needs the self-contained build; drop -Dynamic." }
    Write-Host "Building installer ..." -ForegroundColor Green
    & (Join-Path $root "build-installer.ps1")
    if ($LASTEXITCODE -ne 0) { throw "installer build failed ($LASTEXITCODE)" }
}

# --- optional: run -------------------------------------------------------
if ($Run) {
    Write-Host "Launching typewriter.exe ..." -ForegroundColor Green
    Start-Process -FilePath $exe -WorkingDirectory $root
}
