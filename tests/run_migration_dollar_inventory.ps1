# Static inventory guard for inline find('$') template-hash recovery sites.
#
# Counts may only ratchet downward. Removal boundary: architecture boundary 3B.
#
# Usage:
#   pwsh tests/run_migration_dollar_inventory.ps1
#   pwsh tests/run_migration_dollar_inventory.ps1 -UpdateBaseline

param(
	[switch]$UpdateBaseline = $false
)

$ErrorActionPreference = "Stop"

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoRoot = Split-Path -Parent $scriptDir
Set-Location $repoRoot

$baselinePath = Join-Path $scriptDir "migration_counters\dollar_find_baseline.txt"
$pattern = "find\('\`$'\)"

$matches = Get-ChildItem -LiteralPath (Join-Path $repoRoot "src") -Recurse -File -Include *.cpp,*.h |
	Select-String -Pattern $pattern
$actual = @($matches).Count

if ($UpdateBaseline) {
	[System.IO.File]::WriteAllText($baselinePath, "$actual`n", [System.Text.UTF8Encoding]::new($false))
	Write-Host "Dollar find('$') baseline updated to $actual"
	exit 0
}

if (-not (Test-Path -LiteralPath $baselinePath)) {
	Write-Host "ERROR: Baseline file not found: $baselinePath" -ForegroundColor Red
	exit 1
}

$baseline = [long](Get-Content -LiteralPath $baselinePath -TotalCount 1)
Write-Host "Dollar find('$') inventory: actual=$actual baseline=$baseline"
if ($actual -gt $baseline) {
	Write-Host "RESULT: FAILED - inline dollar recovery inventory increased" -ForegroundColor Red
	exit 1
}
Write-Host "RESULT: OK - dollar inventory within baseline"
exit 0
