$ErrorActionPreference = "Stop"

# Stamps a self-contained Windows installer with Forge.
#   -> dist/Typewriter-Setup-<version>.exe
#
# Forge reads forge.toml in this folder. Keep its [app] version in step with
# TYPEWRITER_VERSION in main.c. The payload is the static typewriter.exe from
# .\build.ps1 (or `mingw32-make STATIC=1`); run that first, or use
# .\build.ps1 -Installer for both steps.

$scriptDir = $PSScriptRoot
$workDir   = Split-Path $scriptDir -Parent
$forge     = Join-Path $workDir "Forge\build\forge.exe"

if (-not (Test-Path $forge)) {
    Write-Host "forge.exe not found at: $forge" -ForegroundColor Red
    Write-Host "Build it first: run 'gobake build' in the Forge project." -ForegroundColor Yellow
    exit 1
}

$exe = Join-Path $scriptDir "typewriter.exe"
if (-not (Test-Path $exe)) {
    Write-Host "typewriter.exe not found - run .\build.ps1 first." -ForegroundColor Red
    exit 1
}

# forge.exe is linked with -H windowsgui. PowerShell's call operator does NOT wait
# for a GUI-subsystem process: `& $forge build` returns immediately, so the script
# would race ahead to look for an installer that was still being written, and
# $LASTEXITCODE would never be set -- meaning a genuine forge failure reads as
# success. Start-Process -Wait gives us both the wait and a real exit code.
function Invoke-Forge {
    param([Parameter(Mandatory = $true)][string[]] $ForgeArgs)

    $outFile = [System.IO.Path]::GetTempFileName()
    $errFile = [System.IO.Path]::GetTempFileName()
    try {
        $proc = Start-Process -FilePath $forge -ArgumentList $ForgeArgs `
            -NoNewWindow -Wait -PassThru `
            -RedirectStandardOutput $outFile -RedirectStandardError $errFile

        Get-Content $outFile -ErrorAction SilentlyContinue | ForEach-Object { Write-Host $_ }
        Get-Content $errFile -ErrorAction SilentlyContinue | ForEach-Object { Write-Host $_ -ForegroundColor Yellow }

        return $proc.ExitCode
    }
    finally {
        Remove-Item $outFile, $errFile -Force -ErrorAction SilentlyContinue
    }
}

Write-Host "Using $forge" -ForegroundColor DarkGray
Write-Host ("Payload: {0} ({1} MB)" -f $exe, [math]::Round((Get-Item $exe).Length / 1MB, 1)) -ForegroundColor DarkGray

Push-Location $scriptDir
try {
    # Validate the manifest before bundling.
    if ((Invoke-Forge @("validate")) -ne 0) {
        Write-Host "forge.toml is invalid." -ForegroundColor Red
        exit 1
    }

    Write-Host ""
    Write-Host "Building installer..." -ForegroundColor Cyan
    if ((Invoke-Forge @("build")) -ne 0) {
        Write-Host "Installer build failed." -ForegroundColor Red
        exit 1
    }
}
finally {
    Pop-Location
}

Write-Host ""
$built = @(Get-ChildItem (Join-Path $scriptDir "dist") -Filter "Typewriter-Setup-*.exe" -ErrorAction SilentlyContinue |
           Sort-Object LastWriteTime -Descending | Select-Object -First 1)
if ($built.Count -eq 0) {
    Write-Host "forge build reported success but produced no installer." -ForegroundColor Red
    exit 1
}

$built | ForEach-Object {
    Write-Host ("Installer: {0} ({1} MB)" -f $_.FullName, [math]::Round($_.Length / 1MB, 1)) -ForegroundColor Green
}
