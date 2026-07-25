# Print global operator new/delete stats from a compile with --alloc-stats.
# Requires a Sharded build compiled with FLASHCPP_TRACK_ALLOCATIONS=1.
# Enable with: .\build_flashcpp.bat --alloc-stats
#
# Usage:
#   pwsh tests/bench_allocations.ps1 [test.cpp]
#
# Example:
#   .\build_flashcpp.bat
#   pwsh tests/bench_allocations.ps1 tests/std/test_std_type_traits.cpp

param(
	[Parameter(Position = 0)]
	[string]$TestFile = "tests/std/test_std_type_traits.cpp"
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

Write-Host "Running allocation benchmark: $TestPath"
$output = & $Compiler --alloc-stats $TestPath 2>&1 | Out-String

if ($LASTEXITCODE -ne 0) {
	Write-Host $output
	throw "Compile failed with exit code $LASTEXITCODE"
}

$globalMatch = [regex]::Match(
	$output,
	"allocations=(\d+), deallocations=(\d+), bytes allocated=(\d+), bytes deallocated=(\d+)"
)
if (-not $globalMatch.Success) {
	$allocMatch = [regex]::Match($output, "allocations=(\d+)")
	$bytesMatch = [regex]::Match($output, "bytes allocated=(\d+)")
	if (-not $allocMatch.Success -or -not $bytesMatch.Success) {
		Write-Host $output
		throw "Could not parse allocation stats (rebuild with FLASHCPP_TRACK_ALLOCATIONS=1 and pass --alloc-stats)"
	}
	$totalAllocations = [uint64]$allocMatch.Groups[1].Value
	$totalDeallocations = 0
	$totalBytes = [uint64]$bytesMatch.Groups[1].Value
	$totalBytesDeallocated = 0
} else {
	$totalAllocations = [uint64]$globalMatch.Groups[1].Value
	$totalDeallocations = [uint64]$globalMatch.Groups[2].Value
	$totalBytes = [uint64]$globalMatch.Groups[3].Value
	$totalBytesDeallocated = [uint64]$globalMatch.Groups[4].Value
}

Write-Host ""
Write-Host "=== Allocation Summary ==="
Write-Host ("allocations={0}" -f $totalAllocations)
Write-Host ("bytes_allocated={0}" -f $totalBytes)
if ($totalDeallocations -gt 0) {
	Write-Host ("deallocations={0}" -f $totalDeallocations)
	Write-Host ("bytes_deallocated={0}" -f $totalBytesDeallocated)
	Write-Host ("live_bytes={0}" -f ($totalBytes - $totalBytesDeallocated))
}

$phasePattern = '(?m)^\s+(?<phase>[^:]+): allocations=(?<count>\d+) \((?<countpct>[\d.]+)%\), bytes=(?<bytes>\d+) \((?<bytespct>[\d.]+)%\), mean=(?<mean>[\d.]+) bytes'
$phaseMatches = [regex]::Matches($output, $phasePattern)
if ($phaseMatches.Count -eq 0) {
	Write-Host ""
	Write-Host "Phase breakdown unavailable (rebuild with phase-tagged allocation tracking)."
	exit 0
}

$phaseRows = @()
foreach ($match in $phaseMatches) {
	$phaseRows += [pscustomobject]@{
		Phase = $match.Groups["phase"].Value.Trim()
		Allocations = [uint64]$match.Groups["count"].Value
		CountPct = [double]$match.Groups["countpct"].Value
		Bytes = [uint64]$match.Groups["bytes"].Value
		BytesPct = [double]$match.Groups["bytespct"].Value
		MeanBytes = [double]$match.Groups["mean"].Value
	}
}

Write-Host ""
Write-Host "=== Allocation Totals By Compile Phase ==="
$invariant = [System.Globalization.CultureInfo]::InvariantCulture
Write-Host ("{0,-18} {1,12} {2,8} {3,14} {4,8} {5,8}" -f "Phase", "Allocations", "%Count", "Bytes", "%Bytes", "MeanB")
Write-Host ("{0,-18} {1,12} {2,8} {3,14} {4,8} {5,8}" -f ("-" * 18), ("-" * 12), ("-" * 8), ("-" * 14), ("-" * 8), ("-" * 8))
foreach ($row in $phaseRows) {
	Write-Host ([string]::Format(
		$invariant,
		"{0,-18} {1,12:N0} {2,7:N2}% {3,14:N0} {4,7:N2}% {5,8:N1}",
		$row.Phase,
		$row.Allocations,
		$row.CountPct,
		$row.Bytes,
		$row.BytesPct,
		$row.MeanBytes))
}

$focusedMatch = [regex]::Match(
	$output,
	'parsing-focused remainder: (?<parsing>\d+) parsing allocations \((?<parsingpct>[\d.]+)%\), preprocessing\+lexer\+codegen: (?<nonbottleneck>\d+) \((?<nonbottleneckpct>[\d.]+)%\)'
)
if ($focusedMatch.Success) {
	Write-Host ""
	Write-Host "=== Parsing-Focused View ==="
	Write-Host ([string]::Format(
		$invariant,
		"Parsing allocations:              {0,12:N0} ({1:N2}% of total)",
		[uint64]$focusedMatch.Groups["parsing"].Value,
		[double]$focusedMatch.Groups["parsingpct"].Value))
	Write-Host ([string]::Format(
		$invariant,
		"Preprocessing + Lexer + Codegen:  {0,12:N0} ({1:N2}% of total)",
		[uint64]$focusedMatch.Groups["nonbottleneck"].Value,
		[double]$focusedMatch.Groups["nonbottleneckpct"].Value))

	$midPhases = @("Parsing", "Semantic Analysis", "IR Conversion", "Deferred Gen")
	$midAllocations = ($phaseRows | Where-Object { $midPhases -contains $_.Phase } | Measure-Object -Property Allocations -Sum).Sum
	$midBytes = ($phaseRows | Where-Object { $midPhases -contains $_.Phase } | Measure-Object -Property Bytes -Sum).Sum
	if ($totalAllocations -gt 0) {
		$midAllocPct = 100.0 * $midAllocations / $totalAllocations
		$midBytesPct = 100.0 * $midBytes / $totalBytes
		Write-Host ([string]::Format(
			$invariant,
			"Parsing + Sema + IR + Deferred: {0,12:N0} ({1:N2}% count, {2:N2}% bytes)",
			$midAllocations, $midAllocPct, $midBytesPct))
	}
}
