$ErrorActionPreference = "Stop"
$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoRoot = Split-Path -Parent (Split-Path -Parent $scriptDir)
. (Join-Path $scriptDir "RunnerCommon.ps1")

$failures = 0
function Assert-Runner {
	param([bool]$Condition, [string]$Message)
	if ($Condition) {
		Write-Host "PASS: $Message"
	} else {
		Write-Host "FAIL: $Message" -ForegroundColor Red
		$script:failures++
	}
}

$tempRoot = Join-Path ([System.IO.Path]::GetTempPath()) "flashcpp_runner_self_$PID"
New-Item -ItemType Directory -Path $tempRoot -Force | Out-Null
try {
	$binary = Join-Path $tempRoot "FlashCpp.exe"
	$source = Join-Path $tempRoot "source.cpp"
	Set-Content -LiteralPath $binary -Value "fixture"
	Set-Content -LiteralPath $source -Value "fixture"
	(Get-Item -LiteralPath $binary).LastWriteTimeUtc = [DateTime]::UtcNow.AddMinutes(-2)
	(Get-Item -LiteralPath $source).LastWriteTimeUtc = [DateTime]::UtcNow
	$freshness = Test-FlashCppBinaryFreshness -BinaryPath $binary -SourceFiles @((Get-Item -LiteralPath $source))
	Assert-Runner (-not $freshness.IsFresh -and $freshness.NewestSource -eq $source) "stale compiler timestamps are rejected with the newer source"

	(Get-Item -LiteralPath $binary).LastWriteTimeUtc = [DateTime]::UtcNow.AddMinutes(2)
	$freshness = Test-FlashCppBinaryFreshness -BinaryPath $binary -SourceFiles @((Get-Item -LiteralPath $source))
	Assert-Runner $freshness.IsFresh "fresh compiler timestamps pass"

	$compileOnlyKind = Get-FlashCppTestKind -FileName "test_compile_only.cpp" -SourceContent "int value();" -PlatformExclusions @() -SupportSources @() -CompileOnlyOverrides @()
	$failureKind = Get-FlashCppTestKind -FileName "test_invalid_fail.cpp" -SourceContent "not C++" -PlatformExclusions @() -SupportSources @() -CompileOnlyOverrides @()
	$legacyCompileOnlyKind = Get-FlashCppTestKind -FileName "test_ub_fail.cpp" -SourceContent "int value();" -PlatformExclusions @() -SupportSources @() -CompileOnlyOverrides @("test_ub_fail.cpp")
	Assert-Runner ($compileOnlyKind -eq "CompileOnly") "eligible sources without main are scheduled as compile-only"
	Assert-Runner ($failureKind -eq "CompileFailure") "_fail sources are scheduled even without main"
	Assert-Runner ($legacyCompileOnlyKind -eq "CompileOnly") "explicit legacy compile-only probes are not inferred as expected failures from their suffix"

	$returnCheck = Test-FlashCppLinuxReturnValue -Name "test_invalid_ret256.cpp"
	Assert-Runner (-not $returnCheck.IsValid -and $returnCheck.Value -eq 256) "Linux return encodings above 255 are rejected"

	$successCases = @(Get-FlashCppMultiTuCases -Root (Join-Path $scriptDir "fixtures\multi_tu_success"))
	$invalidCases = @(Get-FlashCppMultiTuCases -Root (Join-Path $scriptDir "fixtures\invalid_multi_tu"))
	Assert-Runner ($successCases.Count -eq 1 -and $successCases[0].Sources.Count -eq 2 -and $null -eq $successCases[0].Error) "valid multi-TU fixtures group all translation units"
	Assert-Runner ($invalidCases.Count -eq 1 -and $null -ne $invalidCases[0].Error) "discovered but unrunnable multi-TU fixtures fail discovery"

	$ciPath = Join-Path $tempRoot "runner.tsv"
	Initialize-FlashCppCiOutput -Path $ciPath
	Write-FlashCppCiRecord -Path $ciPath -Kind "test" -Name "fixture" -Status "failed" -Detail "line one`nline two"
	$ciLines = @(Get-Content -LiteralPath $ciPath)
	Assert-Runner ($ciLines.Count -eq 2 -and $ciLines[1] -match '^flashcpp-runner-v1\ttest\tfixture\tfailed\tline one line two$') "CI records use the stable tab-separated schema"
	$ciBytes = [System.IO.File]::ReadAllBytes($ciPath)
	Assert-Runner (-not ($ciBytes.Count -ge 3 -and $ciBytes[0] -eq 0xEF -and $ciBytes[1] -eq 0xBB -and $ciBytes[2] -eq 0xBF)) "CI records use UTF-8 without a byte-order mark"

	$parsedCount = Get-FlashCppOutsideEngineDiagnosticCount -CompilerOutput "phase timing`nDiagnostics emitted outside DiagnosticEngine: 7`n"
	Assert-Runner ($parsedCount -eq 7) "outside-engine telemetry line parses to its count"
	$missingCount = Get-FlashCppOutsideEngineDiagnosticCount -CompilerOutput "compiler crashed without telemetry"
	Assert-Runner ($null -eq $missingCount) "output without a telemetry line yields no count"

	$okStatus = Test-FlashCppMigrationCounterBaseline -ActualCount 3 -BaselineCount 3
	$improvedStatus = Test-FlashCppMigrationCounterBaseline -ActualCount 2 -BaselineCount 3
	$regressedStatus = Test-FlashCppMigrationCounterBaseline -ActualCount 4 -BaselineCount 3
	$untrackedStatus = Test-FlashCppMigrationCounterBaseline -ActualCount 4 -BaselineCount $null
	Assert-Runner ($okStatus -eq "Ok") "counts equal to the baseline pass"
	Assert-Runner ($improvedStatus -eq "Improved") "counts below the baseline ratchet down without failing"
	Assert-Runner ($regressedStatus -eq "Regressed") "counts above the baseline are regressions"
	Assert-Runner ($untrackedStatus -eq "MissingBaseline") "corpus entries without a baseline cannot pass silently"
} finally {
	Remove-Item -LiteralPath $tempRoot -Recurse -Force -ErrorAction SilentlyContinue
}

if ($failures -gt 0) {
	Write-Host "Runner self-tests failed: $failures" -ForegroundColor Red
	exit 1
}
Write-Host "Runner self-tests passed"
