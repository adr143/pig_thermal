param(
    [string]$HostAddress = "0.0.0.0",
    [int]$Port = 5000
)

$ErrorActionPreference = "Stop"

$ProjectDir = Resolve-Path "$PSScriptRoot\.."
$ServerDir  = Join-Path $ProjectDir "server"

$PythonExe = Join-Path $ServerDir ".venv\Scripts\python.exe"
if (-not (Test-Path $PythonExe)) {
    Write-Error "[run] venv not found at $PythonExe. Run .\scripts\setup.ps1 first."
    exit 1
}

Write-Host "[run] starting Flask backend from $ServerDir" -ForegroundColor Cyan
Write-Host "[run] binding backend to $HostAddress`:$Port" -ForegroundColor Cyan
$env:HOST = $HostAddress
$env:PORT = "$Port"
Set-Location $ServerDir
& $PythonExe app.py
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}
