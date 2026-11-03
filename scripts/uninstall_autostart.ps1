$ErrorActionPreference = "Stop"

foreach ($task in @("PigMonitorBackend", "PigMonitorWeb")) {
    $existing = Get-ScheduledTask -TaskName $task -ErrorAction SilentlyContinue
    if ($existing) {
        Unregister-ScheduledTask -TaskName $task -Confirm:$false
        Write-Host "[uninstall] removed task: $task" -ForegroundColor Green
    } else {
        Write-Host "[uninstall] task not found: $task" -ForegroundColor DarkGray
    }
}

$ProjectDir = Resolve-Path "$PSScriptRoot\.."
foreach ($w in @("_run_backend_wrapper.ps1", "_run_web_wrapper.ps1")) {
    $p = Join-Path $ProjectDir "scripts\$w"
    if (Test-Path $p) { Remove-Item $p -Force }
}
