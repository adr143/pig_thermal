# ============================================================
#  Start the Flask backend on Windows.
#    Dev mode (default):  .\scripts\run_backend.ps1
#    Prod mode (debug disabled): $env:PIGMON_PROD='1'; .\scripts\run_backend.ps1
# ============================================================
$ErrorActionPreference = "Stop"

$ProjectDir = Resolve-Path "$PSScriptRoot\.."
$ServerDir  = Join-Path $ProjectDir "server"

$PythonExe = Join-Path $ServerDir ".venv\Scripts\python.exe"
if (-not (Test-Path $PythonExe)) {
    Write-Error "[run] venv not found at $PythonExe. Run .\scripts\setup.ps1 first."
    exit 1
}

Write-Host "[run] starting Flask backend from $ServerDir" -ForegroundColor Cyan
Set-Location $ServerDir
& $PythonExe app.py
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}
