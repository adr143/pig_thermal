
$ErrorActionPreference = "Stop"

$ProjectDir  = Resolve-Path "$PSScriptRoot\.."
$ServerDir   = Join-Path $ProjectDir "server"
$WebDir      = Join-Path $ProjectDir "web"

Write-Host "[setup] project: $ProjectDir"

if (-not (Get-Command python -ErrorAction SilentlyContinue)) {
    Write-Error "[setup] python not found. Install Python 3.10+ from https://python.org (check 'Add to PATH' during install)."
    exit 1
}

Write-Host "[setup] creating python venv at $ServerDir\.venv"
python -m venv "$ServerDir\.venv"
if ($LASTEXITCODE -ne 0) {
    throw "[setup] failed to create the Python virtual environment."
}

$PythonExe = Join-Path $ServerDir ".venv\Scripts\python.exe"
& $PythonExe -m pip install --upgrade pip
if ($LASTEXITCODE -ne 0) {
    throw "[setup] failed to upgrade pip."
}
Write-Host "[setup] installing python deps (this may take 5-10 minutes for torch)"
& $PythonExe -m pip install -r "$ServerDir\requirements.txt"
if ($LASTEXITCODE -ne 0) {
    throw "[setup] failed to install Python dependencies."
}

if (-not (Test-Path "$ServerDir\.env")) {
    Write-Host "[setup] copying server\.env.example -> server\.env"
    Copy-Item "$ServerDir\.env.example" "$ServerDir\.env"
}

New-Item -ItemType Directory -Force -Path "$ServerDir\snapshots" | Out-Null
New-Item -ItemType Directory -Force -Path "$ServerDir\models"   | Out-Null

if (Get-Command npm -ErrorAction SilentlyContinue) {
    Write-Host "[setup] installing next.js deps"
    Push-Location $WebDir
    try {
        npm install --no-audit --no-fund
        if ($LASTEXITCODE -ne 0) {
            throw "[setup] failed to install web dependencies."
        }
    } finally {
        Pop-Location
    }
} else {
    Write-Warning "[setup] npm not found. Install Node.js 18+ (LTS) from https://nodejs.org and re-run setup."
}

if (-not (Test-Path "$WebDir\.env.local")) {
    Write-Host "[setup] copying web\.env.example -> web\.env.local"
    Copy-Item "$WebDir\.env.example" "$WebDir\.env.local"
}

Write-Host ""
Write-Host "[setup] done." -ForegroundColor Green
Write-Host ""
Write-Host "Next steps:"
Write-Host "  1. Drop your .pt models into server\models\ (pig_detect.pt, pig_classify.pt)"
Write-Host "  2. Configure Wi-Fi credentials and WS_HOST in arduino\esp32_thermal_cam\secrets.h"
Write-Host "  3. Flash the ESP32 sketch from arduino\esp32_thermal_cam\"
Write-Host "  4. Start the backend:  .\scripts\run_backend.ps1"
Write-Host "  5. Start the UI:       .\scripts\run_web.ps1"
Write-Host "  6. Open http://localhost:3000 in your browser"
Write-Host ""
Write-Host "Optional: auto-start on boot:  .\scripts\install_autostart.ps1"
