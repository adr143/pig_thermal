
param(
    [switch]$AtBoot = $false
)

$ErrorActionPreference = "Stop"

$ProjectDir = Resolve-Path "$PSScriptRoot\.."
$ServerDir  = Join-Path $ProjectDir "server"
$WebDir     = Join-Path $ProjectDir "web"

$PythonExe = Join-Path $ServerDir ".venv\Scripts\python.exe"
$NpmCommand = Get-Command npm -ErrorAction SilentlyContinue
if (-not $NpmCommand) {
    Write-Error "npm not found. Install Node.js 18+ and run .\scripts\setup.ps1 first."
    exit 1
}
$NpmExe = $NpmCommand.Source

if (-not (Test-Path $PythonExe)) {
    Write-Error "venv python not found at $PythonExe. Run .\scripts\setup.ps1 first."
    exit 1
}

$BackendWrapper = Join-Path $ProjectDir "scripts\_run_backend_wrapper.ps1"
@"
Set-Location '$ServerDir'
& '$PythonExe' app.py
"@ | Set-Content -Path $BackendWrapper -Encoding UTF8

$WebWrapper = Join-Path $ProjectDir "scripts\_run_web_wrapper.ps1"
@"
Set-Location '$WebDir'
& '$NpmExe' run dev
"@ | Set-Content -Path $WebWrapper -Encoding UTF8

if ($AtBoot) {
    $Trigger = New-ScheduledTaskTrigger -AtStartup
    $Principal = New-ScheduledTaskPrincipal -UserId "NT AUTHORITY\SYSTEM" -LogonType ServiceAccount -RunLevel Highest
    Write-Host "[install] creating tasks to start at BOOT (needs admin)" -ForegroundColor Yellow
} else {
    $Trigger = New-ScheduledTaskTrigger -AtLogOn
    $Principal = New-ScheduledTaskPrincipal -UserId $env:USERNAME -LogonType Interactive -RunLevel Limited
    Write-Host "[install] creating tasks to start at LOGON" -ForegroundColor Cyan
}

$Settings = New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries -StartWhenAvailable -RestartCount 5 -RestartInterval (New-TimeSpan -Minutes 1)

$BackendAction = New-ScheduledTaskAction `
    -Execute "powershell.exe" `
    -Argument "-NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File `"$BackendWrapper`""

Register-ScheduledTask `
    -TaskName "PigMonitorBackend" `
    -Action $BackendAction `
    -Trigger $Trigger `
    -Principal $Principal `
    -Settings $Settings `
    -Description "Pig Monitoring System — Flask backend (port 5000)" `
    -Force | Out-Null

$WebAction = New-ScheduledTaskAction `
    -Execute "powershell.exe" `
    -Argument "-NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File `"$WebWrapper`""

Register-ScheduledTask `
    -TaskName "PigMonitorWeb" `
    -Action $WebAction `
    -Trigger $Trigger `
    -Principal $Principal `
    -Settings $Settings `
    -Description "Pig Monitoring System — Next.js web UI (port 3000)" `
    -Force | Out-Null

Write-Host ""
Write-Host "[install] done. Tasks created:" -ForegroundColor Green
Write-Host "  - PigMonitorBackend  (Flask, port 5000)"
Write-Host "  - PigMonitorWeb      (Next.js, port 3000)"
Write-Host ""
Write-Host "Manage them:"
Write-Host "  Get-ScheduledTask -TaskName 'PigMonitor*'"
Write-Host "  Start-ScheduledTask -TaskName 'PigMonitorBackend'"
Write-Host "  Stop-ScheduledTask -TaskName 'PigMonitorBackend'"
Write-Host "  Unregister-ScheduledTask -TaskName 'PigMonitorBackend' -Confirm:`$false"
