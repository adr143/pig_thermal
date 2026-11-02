#requires -RunAsAdministrator

param(
    [int]$Port = 5000
)

$ErrorActionPreference = "Stop"
$RuleName = "PigMonitoringBackend-TCP-$Port"
$DisplayName = "Pig Monitoring Backend (TCP $Port, private networks)"

$rule = Get-NetFirewallRule -Name $RuleName -ErrorAction SilentlyContinue
if ($rule) {
    Set-NetFirewallRule -Name $RuleName -Enabled True -Profile Private
    Write-Host "[firewall] enabled existing private-network rule for TCP port $Port."
} else {
    New-NetFirewallRule `
        -Name $RuleName `
        -DisplayName $DisplayName `
        -Direction Inbound `
        -Action Allow `
        -Protocol TCP `
        -LocalPort $Port `
        -Profile Private | Out-Null
    Write-Host "[firewall] allowed inbound TCP port $Port on private networks."
}
