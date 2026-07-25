# Measure global operator new/delete stats via --perf-stats.
# Requires a Sharded build compiled with FLASHCPP_TRACK_ALLOCATIONS=1.
#
# Usage:
#   pwsh tests/bench_allocations.ps1 [test.cpp] [-UpdateBaseline]
#
# Example:
#   .\build_flashcpp.bat
#   pwsh tests/bench_allocations.ps1 tests/std/test_std_type_traits.cpp

param(
	[Parameter(Position = 0)]
	[string]$TestFile = "tests/std/test_std_type_traits.cpp",
	[switch]$UpdateBaseline
)

$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RepoRoot = Split-Path -Parent $ScriptDir
Set-Location $RepoRoot

$Compiler = Join-Path $RepoRoot "x64\Sharded\FlashCppMSVC.exe"
if (-not (Test-Path $Compiler)) {
	Write-Error "Compiler not found: $Compiler (run .\build_flashcpp.bat first)"
}

$TestPath = Resolve-Path $TestFile
$TestLeaf = Split-Path $TestPath -Leaf
$BaselinePath = Join-Path $ScriptDir "baselines\$($TestLeaf -replace '\.cpp$','')_allocations.txt"

Write-Host "Running allocation benchmark: $TestPath"
$output = & $Compiler --perf-stats $TestPath 2>&1 | Out-String

if ($LASTEXITCODE -ne 0) {
	Write-Host $output
	throw "Compile failed with exit code $LASTEXITCODE"
}

$allocMatch = [regex]::Match($output, "allocations=(\d+)")
$bytesMatch = [regex]::Match($output, "bytes allocated=(\d+)")
if (-not $allocMatch.Success -or -not $bytesMatch.Success) {
	Write-Host $output
	throw "Could not parse allocation stats (rebuild with FLASHCPP_TRACK_ALLOCATIONS=1 and pass --perf-stats)"
}

$allocations = [int64]$allocMatch.Groups[1].Value
$bytesAllocated = [int64]$bytesMatch.Groups[1].Value

Write-Host "allocations=$allocations"
Write-Host "bytes_allocated=$bytesAllocated"

if ($UpdateBaseline) {
	@(
		"# Baseline for $TestFile with --perf-stats"
		"# Build: Sharded with FLASHCPP_TRACK_ALLOCATIONS=1 (stacks optional/off)"
		"allocations=$allocations"
		"bytes_allocated=$bytesAllocated"
		""
	) | Set-Content -Path $BaselinePath -Encoding utf8
	Write-Host "Updated baseline: $BaselinePath"
	exit 0
}

if (-not (Test-Path $BaselinePath)) {
	Write-Warning "No baseline file at $BaselinePath (use -UpdateBaseline to create one)"
	exit 0
}

$baselineAlloc = $null
$baselineBytes = $null
Get-Content $BaselinePath | ForEach-Object {
	if ($_ -match '^allocations=(\d+)$') { $baselineAlloc = [int64]$Matches[1] }
	if ($_ -match '^bytes_allocated=(\d+)$') { $baselineBytes = [int64]$Matches[1] }
}

if ($null -eq $baselineAlloc -or $null -eq $baselineBytes) {
	throw "Baseline file is missing allocations= or bytes_allocated= entries: $BaselinePath"
}

$allocDeltaPct = (($allocations - $baselineAlloc) * 100.0) / $baselineAlloc
$bytesDeltaPct = (($bytesAllocated - $baselineBytes) * 100.0) / $baselineBytes

Write-Host "Baseline allocations=$baselineAlloc ($('{0:N2}' -f $allocDeltaPct)% delta)"
Write-Host "Baseline bytes_allocated=$baselineBytes ($('{0:N2}' -f $bytesDeltaPct)% delta)"

# Informational only: non-zero exit on large regressions.
$maxRegressionPct = 5.0
if ($allocDeltaPct -gt $maxRegressionPct -or $bytesDeltaPct -gt $maxRegressionPct) {
	Write-Warning "Allocation regression exceeds ${maxRegressionPct}% vs baseline"
	exit 1
}

exit 0
