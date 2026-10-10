# Start EasyTHS server (Tonghuashun GUI automation). Prefer local vendored tree.
$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
$Local = Join-Path $Root "third_party\easyths"
Set-Location $Root

Write-Host "EasyTHS: ensure xiadan.exe is logged in first." -ForegroundColor Yellow

function Have-Cmd($name) {
  return [bool](Get-Command $name -ErrorAction SilentlyContinue)
}

if (Have-Cmd "uvx") {
  Write-Host "Starting via uvx easyths[server] ..."
  & uvx "easyths[server]"
  exit $LASTEXITCODE
}

if (Have-Cmd "uv" -and (Test-Path (Join-Path $Local "pyproject.toml"))) {
  Write-Host "Starting via uv run in third_party/easyths ..."
  Set-Location $Local
  & uv run easyths
  exit $LASTEXITCODE
}

if (Have-Cmd "python") {
  Write-Host "Trying: pip install -e third_party/easyths[server] then easyths"
  & python -m pip install -e "$Local[server]"
  & easyths
  exit $LASTEXITCODE
}

Write-Error "Need uvx/uv/python. Install uv: https://github.com/astral-sh/uv"
