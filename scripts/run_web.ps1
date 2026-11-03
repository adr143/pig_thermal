$ErrorActionPreference = "Stop"

$ProjectDir = Resolve-Path "$PSScriptRoot\.."
$WebDir    = Join-Path $ProjectDir "web"

if (-not (Test-Path "$WebDir\node_modules")) {
    Write-Error "[run] node_modules not found. Run .\scripts\setup.ps1 first."
    exit 1
}

Write-Host "[run] starting Next.js dev server from $WebDir" -ForegroundColor Cyan
Set-Location $WebDir
npm run dev
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}
