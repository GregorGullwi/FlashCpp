# Migration counter baseline check for FlashCpp (PowerShell)
#
# Enforces directional compatibility counters over a fixed corpus
# (front-end rearchitecture, architecture boundary 0). Each corpus entry
# records the number of diagnostics emitted outside DiagnosticEngine for a
# --perf-stats run. Counts may only ratchet downward; an increase fails the
# run. Removal boundary for this counter is architecture boundary 11.
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
$outsideEngineLine = "Diagnostics emitted outside DiagnosticEngine"

Write-Host "=============================================="
Write-Host "FlashCpp Migration Counter Check"
Write-Host "=============================================="
Write-Host ""

# Find the newest compiler executable, mirroring run_all_tests.ps1.
$flashCppPath = ""
$allExes = Get-ChildItem -Path "x64" -Recurse -Include "FlashCpp.exe","FlashCppMSVC.exe" -ErrorAction SilentlyContinue
if ($allExes) {
	$newestExe = $allExes | Sort-Object LastWriteTime -Descending | Select-Object -First 1
	$flashCppPath = $newestExe.FullName
} else {
	Write-Host "ERROR: FlashCpp compiler not found under x64/. Run .\build_flashcpp.bat first." -ForegroundColor Red
	exit 1
}
$flashCppPath = (Get-Item $flashCppPath).FullName

$freshness = Test-FlashCppBinaryFreshness -BinaryPath $flashCppPath -SourceFiles @(Get-FlashCppRelevantSourceFiles -RepoRoot $repoRoot)
if (-not $freshness.IsFresh) {
	$message = "Compiler binary is older than $($freshness.NewestSource). Run .\build_flashcpp.bat and retry."
	Write-Host "ERROR: $message" -ForegroundColor Red
	exit 1
}

if (-not (Test-Path -LiteralPath $baselinePath)) {
	Write-Host "ERROR: Baseline file not found: $baselinePath" -ForegroundColor Red
	exit 1
}

# Baseline entries are "count<TAB>path" relative to the repository root.
$entries = @()
foreach ($line in Get-Content -LiteralPath $baselinePath) {
	if ([string]::IsNullOrWhiteSpace($line) -or $line.StartsWith("#")) { continue }
	$parts = $line -split "`t"
	if ($parts.Count -lt 2) {
		Write-Host "ERROR: Malformed baseline line: $line" -ForegroundColor Red
		exit 1
	}
	$count = 0L
	if (-not [long]::TryParse($parts[0], [ref]$count)) {
		Write-Host "ERROR: Malformed count in baseline line: $line" -ForegroundColor Red
		exit 1
	}
	$entries += [pscustomobject]@{ Path = $parts[1].Trim(); Baseline = $count }
}
if ($entries.Count -eq 0) {
	Write-Host "ERROR: Baseline file contains no corpus entries." -ForegroundColor Red
	exit 1
}

$tempDir = Join-Path ([System.IO.Path]::GetTempPath()) "flashcpp_counters_$PID"
New-Item -ItemType Directory -Path $tempDir -Force | Out-Null

$regressions = @()
$improvements = @()
$unmeasurable = @()
$measurements = @()
$index = 0

try {
	foreach ($entry in ($entries | Sort-Object Path)) {
		$sourcePath = Join-Path $repoRoot $entry.Path
		if (-not (Test-Path -LiteralPath $sourcePath)) {
			$unmeasurable += "$($entry.Path) (missing source file)"
			continue
		}
		$index++
		$objPath = Join-Path $tempDir "corpus_$index.obj"
		$output = & $flashCppPath --perf-stats -o $objPath $sourcePath 2>&1 | Out-String
		$actual = Get-FlashCppOutsideEngineDiagnosticCount -CompilerOutput $output
		if ($null -eq $actual) {
			$unmeasurable += "$($entry.Path) (no telemetry line; compiler crashed or exited early)"
			continue
		}
		$measurements += [pscustomobject]@{ Path = $entry.Path; Actual = $actual; Baseline = $entry.Baseline }
		$status = Test-FlashCppMigrationCounterBaseline -ActualCount $actual -BaselineCount $entry.Baseline
		switch ($status) {
			"Regressed" { $regressions += "$($entry.Path): baseline $($entry.Baseline), now $actual" }
			"Improved" { $improvements += "$($entry.Path): baseline $($entry.Baseline), now $actual" }
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
	$lines = @("# FlashCpp migration counter baseline.")
	foreach ($measurement in ($measurements | Sort-Object Path)) {
		$lines += "$($measurement.Actual)`t$($measurement.Path)"
	}
	# LF endings keep the regenerated file byte-identical to the committed one.
	[System.IO.File]::WriteAllText($baselinePath, ($lines -join "`n") + "`n", [System.Text.UTF8Encoding]::new($false))
	Write-Host "Baseline updated with $($measurements.Count) measured entr(ies): $baselinePath"
	exit 0
}

foreach ($measurement in ($measurements | Sort-Object Path)) {
	$status = Test-FlashCppMigrationCounterBaseline -ActualCount $measurement.Actual -BaselineCount $measurement.Baseline
	Write-Host ("{0}  outside-engine={1}  baseline={2}  [{3}]" -f `
		$measurement.Path, $measurement.Actual, $measurement.Baseline, $status)
}

Write-Host ""
if ($unmeasurable.Count -gt 0) {
	Write-Host "=== Unmeasurable corpus entries ===" -ForegroundColor Red
	$unmeasurable | ForEach-Object { Write-Host "  $_" -ForegroundColor Red }
}
if ($regressions.Count -gt 0) {
	Write-Host "=== Counter regressions (legacy diagnostic emissions increased) ===" -ForegroundColor Red
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
