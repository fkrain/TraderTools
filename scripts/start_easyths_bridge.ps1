# Poll orders/outbox and forward to EasyTHS REST.
$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
Set-Location $Root
$py = Get-Command python -ErrorAction SilentlyContinue
if (-not $py) { $py = Get-Command py -ErrorAction SilentlyContinue }
if (-not $py) { Write-Error "python not found"; exit 1 }

$data = Join-Path $Root "data"
Write-Host "Bridge data=$data  (EasyTHS must be on :7648)" -ForegroundColor Cyan
& $py.Source (Join-Path $Root "tools\easyths_bridge\worker.py") --data $data @args
