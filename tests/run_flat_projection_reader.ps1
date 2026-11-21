# Static flat-projection reader inventory under src/.
#
# Per-file counts may only ratchet downward. Removal boundary: architecture P0 gate.
#
# Usage:
#   pwsh tests/run_flat_projection_reader.ps1
#   pwsh tests/run_flat_projection_reader.ps1 -UpdateBaseline

param(
	[switch]$UpdateBaseline = $false
)

$ErrorActionPreference = "Stop"

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoRoot = Split-Path -Parent $scriptDir
Set-Location $repoRoot

$python = Get-Command python -ErrorAction SilentlyContinue
if (-not $python) {
	$python = Get-Command python3 -ErrorAction SilentlyContinue
}
if (-not $python) {
	Write-Host "ERROR: python not found" -ForegroundColor Red
	exit 1
}

$arguments = @("tests/architecture/run_flat_projection_reader.py")
if ($UpdateBaseline) {
	$arguments += "--update-baseline"
}
& $python.Source @arguments
exit $LASTEXITCODE