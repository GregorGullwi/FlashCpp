# Migration counter baseline check for FlashCpp (PowerShell)
#
# Enforces directional compatibility counters over a fixed corpus
# (front-end rearchitecture, architecture boundaries 0 and 4). Counts may
# only ratchet downward; an increase fails the run.
#
# Usage:
#   pwsh tests/run_migration_counters.ps1                 # enforce baseline
#   pwsh tests/run_migration_counters.ps1 -UpdateBaseline # rewrite baseline

param(
	[switch]$UpdateBaseline = $false
)

$ErrorActionPreference = "SilentlyContinue"

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoRoot = Split-Path -Parent $scriptDir
Set-Location $repoRoot
. (Join-Path $scriptDir "runner\RunnerCommon.ps1")

$baselinePath = Join-Path $scriptDir "migration_counters\corpus_baseline.tsv"

Write-Host "=============================================="
Write-Host "FlashCpp Migration Counter Check"
Write-Host "=============================================="
Write-Host ""

$flashCppPath = Resolve-FlashCppCompilerPath -RepoRoot $repoRoot
if (-not $flashCppPath) {
	Write-Host "ERROR: FlashCpp compiler not found under x64/. Run .\build_flashcpp.bat or make sharded first." -ForegroundColor Red
	exit 1
}
$flashCppPath = (Get-Item -LiteralPath $flashCppPath).FullName

$freshness = Test-FlashCppBinaryFreshness -BinaryPath $flashCppPath -SourceFiles @(Get-FlashCppRelevantSourceFiles -RepoRoot $repoRoot)
if (-not $freshness.IsFresh) {
	$message = "Compiler binary is older than $($freshness.NewestSource). Rebuild the compiler and retry."
	Write-Host "ERROR: $message" -ForegroundColor Red
	exit 1
}

if (-not (Test-Path -LiteralPath $baselinePath)) {
	Write-Host "ERROR: Baseline file not found: $baselinePath" -ForegroundColor Red
	exit 1
}

# Baseline format: path<TAB>counter<TAB>count
$entries = @()
foreach ($line in Get-Content -LiteralPath $baselinePath) {
	if ([string]::IsNullOrWhiteSpace($line) -or $line.StartsWith("#")) { continue }
	$parts = $line -split "`t"
	if ($parts.Count -lt 3) {
		Write-Host "ERROR: Malformed baseline line (expected path, counter, count): $line" -ForegroundColor Red
		exit 1
	}
	$count = 0L
	if (-not [long]::TryParse($parts[2], [ref]$count)) {
		Write-Host "ERROR: Malformed count in baseline line: $line" -ForegroundColor Red
		exit 1
	}
	$entries += [pscustomobject]@{
		Path = $parts[0].Trim()
		Counter = $parts[1].Trim()
		Baseline = $count
	}
}
if ($entries.Count -eq 0) {
	Write-Host "ERROR: Baseline file contains no corpus entries." -ForegroundColor Red
	exit 1
}

$uniquePaths = $entries | Select-Object -ExpandProperty Path -Unique
$tempDir = Join-Path ([System.IO.Path]::GetTempPath()) "flashcpp_counters_$PID"
New-Item -ItemType Directory -Path $tempDir -Force | Out-Null

$regressions = @()
$improvements = @()
$unmeasurable = @()
$measurements = @()
$index = 0
$compiledOutput = @{}

try {
	foreach ($path in ($uniquePaths | Sort-Object)) {
		$sourcePath = Join-Path $repoRoot $path
		if (-not (Test-Path -LiteralPath $sourcePath)) {
			$unmeasurable += "$path (missing source file)"
			continue
		}
		$index++
		$objPath = Join-Path $tempDir "corpus_$index.obj"
		$output = & $flashCppPath --perf-stats -o $objPath $sourcePath 2>&1 | Out-String
		$actualValues = Get-FlashCppMigrationCounterValues -CompilerOutput $output
		if ($null -eq $actualValues) {
			$unmeasurable += "$path (missing telemetry; compiler crashed or exited early)"
			continue
		}
		$compiledOutput[$path] = $actualValues
	}

	foreach ($entry in ($entries | Sort-Object Path, Counter)) {
		if (-not $compiledOutput.ContainsKey($entry.Path)) { continue }
		$actualValues = $compiledOutput[$entry.Path]
		if (-not $actualValues.ContainsKey($entry.Counter)) {
			$unmeasurable += "$($entry.Path) ($($entry.Counter) telemetry missing)"
			continue
		}
		$actual = $actualValues[$entry.Counter]
		$measurements += [pscustomobject]@{
			Path = $entry.Path
			Counter = $entry.Counter
			Actual = $actual
			Baseline = $entry.Baseline
		}
		$status = Test-FlashCppMigrationCounterBaseline -ActualCount $actual -BaselineCount $entry.Baseline
		switch ($status) {
			"Regressed" { $regressions += "$($entry.Path) [$($entry.Counter)]: baseline $($entry.Baseline), now $actual" }
			"Improved" { $improvements += "$($entry.Path) [$($entry.Counter)]: baseline $($entry.Baseline), now $actual" }
		}
	}
} finally {
	Remove-Item -LiteralPath $tempDir -Recurse -Force -ErrorAction SilentlyContinue
}

if ($UpdateBaseline) {
	if ($unmeasurable.Count -gt 0) {
		Write-Host "Refusing to update the baseline while corpus entries are unmeasurable:" -ForegroundColor Red
		$unmeasurable | ForEach-Object { Write-Host "  $_" -ForegroundColor Red }
		exit 1
	}
	$lines = @("# FlashCpp migration counter baseline (path<TAB>counter<TAB>count).")
	foreach ($path in ($compiledOutput.Keys | Sort-Object)) {
		foreach ($counter in ($compiledOutput[$path].Keys | Sort-Object)) {
			$lines += "$path`t$counter`t$($compiledOutput[$path][$counter])"
		}
	}
	[System.IO.File]::WriteAllText($baselinePath, ($lines -join "`n") + "`n", [System.Text.UTF8Encoding]::new($false))
	Write-Host "Baseline updated with $($lines.Count - 1) measured entr(ies): $baselinePath"
	exit 0
}

foreach ($measurement in ($measurements | Sort-Object Path, Counter)) {
	$status = Test-FlashCppMigrationCounterBaseline -ActualCount $measurement.Actual -BaselineCount $measurement.Baseline
	Write-Host ("{0}  {1}={2}  baseline={3}  [{4}]" -f `
		$measurement.Path, $measurement.Counter, $measurement.Actual, $measurement.Baseline, $status)
}

Write-Host ""
if ($unmeasurable.Count -gt 0) {
	Write-Host "=== Unmeasurable corpus entries ===" -ForegroundColor Red
	$unmeasurable | ForEach-Object { Write-Host "  $_" -ForegroundColor Red }
}
if ($regressions.Count -gt 0) {
	Write-Host "=== Counter regressions ===" -ForegroundColor Red
	$regressions | ForEach-Object { Write-Host "  $_" -ForegroundColor Red }
}
if ($improvements.Count -gt 0) {
	Write-Host "Improvements recorded (lower the baseline with -UpdateBaseline):" -ForegroundColor Yellow
	$improvements | ForEach-Object { Write-Host "  $_" -ForegroundColor Yellow }
}

if ($regressions.Count -gt 0 -or $unmeasurable.Count -gt 0) {
	Write-Host ""
	Write-Host "RESULT: FAILED - migration counters moved in the wrong direction or became unmeasurable" -ForegroundColor Red
	exit 1
}
Write-Host "RESULT: OK - all migration counters within baseline"
exit 0
