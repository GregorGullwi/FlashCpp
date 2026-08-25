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
	$failureKind = Get-FlashCppTestKind -FileName "test_invalid_e1001.cpp" -SourceContent "int main() { return 0; }" -PlatformExclusions @() -SupportSources @() -CompileOnlyOverrides @()
	Assert-Runner ($compileOnlyKind -eq "CompileOnly") "eligible sources without main are scheduled as compile-only"
	Assert-Runner ($failureKind -eq "CompileFailure") "encoded negative names are classified before main detection"

	$malformedNames = @(
		"test_invalid_e01.cpp",
		"test_invalid_e0.cpp",
		"test_invalid_e+1.cpp",
		"test_invalid_e.cpp",
		"test_invalid_e1001_e01.cpp"
	)
	$malformedNamesRejected = @(
		$malformedNames | Where-Object {
			(Get-FlashCppNegativeNameInfo -FileName $_).Kind -eq "Malformed"
		}
	).Count -eq $malformedNames.Count
	Assert-Runner $malformedNamesRejected "malformed diagnostic-looking terminal names are rejected"
	$repeatedName = Get-FlashCppNegativeNameInfo -FileName "test_invalid_e1001_e1001_e1051.cpp"
	Assert-Runner ($repeatedName.Kind -eq "Encoded" -and
		($repeatedName.ExpectedIds -join ",") -eq "1001,1001,1051") "filename extraction preserves repeated diagnostic IDs"

	$returnCheck = Test-FlashCppLinuxReturnValue -Name "test_invalid_ret256.cpp"
	Assert-Runner (-not $returnCheck.IsValid -and $returnCheck.Value -eq 256) "Linux return encodings above 255 are rejected"

	$successCases = @(Get-FlashCppMultiTuCases -Root (Join-Path $scriptDir "fixtures\multi_tu_success"))
	$invalidCases = @(Get-FlashCppMultiTuCases -Root (Join-Path $scriptDir "fixtures\invalid_multi_tu"))
	Assert-Runner ($successCases.Count -eq 1 -and $successCases[0].Sources.Count -eq 2 -and $null -eq $successCases[0].Error) "valid multi-TU fixtures group all translation units"
	Assert-Runner ($invalidCases.Count -eq 1 -and $null -ne $invalidCases[0].Error) "discovered but unrunnable multi-TU fixtures fail discovery"

	$ciPath = Join-Path $tempRoot "runner.tsv"
	Initialize-FlashCppCiOutput -Path $ciPath
	Write-FlashCppCiRecord -Path $ciPath -Kind "test" -Name "fixture" -Status "failed" -Detail "line one`nline two"
	Write-FlashCppCiRecord -Path $ciPath -Kind "compatibility" -Name "legacy-internal-failure" -Status "active" -Detail "count=7 baseline=7 selected=0 direction=down removal-boundary=2F"
	$ciLines = @(Get-Content -LiteralPath $ciPath)
	Assert-Runner ($ciLines.Count -eq 3 -and
		$ciLines[1] -match '^flashcpp-runner-v1\ttest\tfixture\tfailed\tline one line two$' -and
		$ciLines[2] -match '^flashcpp-runner-v1\tcompatibility\tlegacy-internal-failure\tactive\tcount=7 baseline=7 selected=0 direction=down removal-boundary=2F$') "CI records include the directional compatibility count in the stable tab-separated schema"
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

	$diagnosticOutput = @(
		"$([char]0x1b)[31m[ERROR][Parser] C:\src\t.cpp:3:7: error: decorated copy [SomeDiagnostic#1001]$([char]0x1b)[0m",
		"C:\src\t.cpp:3:7: error: plain primary [SomeDiagnostic#1001]",
		"C:\src\t.cpp:4:2: warning: plain repeated diagnostic [OtherName#1001]",
		"C:\src\t.cpp:5:9: error: plain note-role replacement [SomeNote#1051]",
		"  in instantiation of 'X' requested here",
		"[Progress] Preprocessing complete: 9 lines"
	) -join "`n"
	$emittedIds = @(Get-FlashCppPlainDiagnosticIds -CompilerOutput $diagnosticOutput)
	Assert-Runner (($emittedIds -join ",") -eq "1001,1001,1051") "plain diagnostics contribute only ID numbers while decorated copies are excluded"

	$matchResult = Compare-FlashCppDiagnosticIdMultisets -Expected $repeatedName.ExpectedIds -Emitted $emittedIds
	Assert-Runner $matchResult.Matched "identical diagnostic ID multisets match"

	$mismatchResult = Compare-FlashCppDiagnosticIdMultisets -Expected $repeatedName.ExpectedIds -Emitted @(1001, 1051, 1051)
	Assert-Runner (-not $mismatchResult.Matched -and
		$mismatchResult.Missing[0] -eq "1001 x1" -and
		$mismatchResult.Excess[0] -eq "1051 x1") "missing and excess diagnostic occurrences are counted"

	$internalResult = Test-FlashCppNegativeCompileResult -FileName "test_invalid_e1001_e1001_e1051.cpp" `
		-Started $true -TimedOut $false -ExitCode $script:FlashCppInternalFailureExit -ObjectExists $false `
		-CompilerOutput $diagnosticOutput -SourceRejectionExit $script:FlashCppSourceRejectionExit `
		-InternalFailureExit $script:FlashCppInternalFailureExit -LegacyInternalCompatibilityNames @() `
		-LegacyInternalCompatibilityRemovalBoundary $script:FlashCppLegacyInternalCompatibilityRemovalBoundary
	Assert-Runner ($internalResult.Status -eq "Bad" -and $internalResult.Detail -match "internal failure") "internal compiler status cannot pass even when expected IDs were emitted"

	$compatibilityName = "test_constexpr_aggregate_brace_narrowing_fail.cpp"
	$legacyInternalResult = Test-FlashCppNegativeCompileResult -FileName $compatibilityName `
		-Started $true -TimedOut $false -ExitCode $script:FlashCppInternalFailureExit -ObjectExists $false `
		-CompilerOutput "" -SourceRejectionExit $script:FlashCppSourceRejectionExit `
		-InternalFailureExit $script:FlashCppInternalFailureExit -LegacyInternalCompatibilityNames @($compatibilityName) `
		-LegacyInternalCompatibilityRemovalBoundary $script:FlashCppLegacyInternalCompatibilityRemovalBoundary
	Assert-Runner ($legacyInternalResult.Status -eq "LegacyInternalCompatibility") "a listed legacy _fail test may use the temporary internal-failure compatibility"

	$unlistedInternalResult = Test-FlashCppNegativeCompileResult -FileName "unlisted_legacy_fail.cpp" `
		-Started $true -TimedOut $false -ExitCode $script:FlashCppInternalFailureExit -ObjectExists $false `
		-CompilerOutput "" -SourceRejectionExit $script:FlashCppSourceRejectionExit `
		-InternalFailureExit $script:FlashCppInternalFailureExit -LegacyInternalCompatibilityNames @($compatibilityName) `
		-LegacyInternalCompatibilityRemovalBoundary $script:FlashCppLegacyInternalCompatibilityRemovalBoundary
	Assert-Runner ($unlistedInternalResult.Status -eq "Bad") "an unlisted legacy _fail test cannot use internal-failure compatibility"

	$encodedInternalResult = Test-FlashCppNegativeCompileResult -FileName "test_constexpr_aggregate_brace_narrowing_e1001.cpp" `
		-Started $true -TimedOut $false -ExitCode $script:FlashCppInternalFailureExit -ObjectExists $false `
		-CompilerOutput $diagnosticOutput -SourceRejectionExit $script:FlashCppSourceRejectionExit `
		-InternalFailureExit $script:FlashCppInternalFailureExit -LegacyInternalCompatibilityNames @($compatibilityName) `
		-LegacyInternalCompatibilityRemovalBoundary $script:FlashCppLegacyInternalCompatibilityRemovalBoundary
	Assert-Runner ($encodedInternalResult.Status -eq "Bad") "an encoded _e test cannot use legacy internal-failure compatibility"

	$compatibilityObjectResult = Test-FlashCppNegativeCompileResult -FileName $compatibilityName `
		-Started $true -TimedOut $false -ExitCode $script:FlashCppInternalFailureExit -ObjectExists $true `
		-CompilerOutput "" -SourceRejectionExit $script:FlashCppSourceRejectionExit `
		-InternalFailureExit $script:FlashCppInternalFailureExit -LegacyInternalCompatibilityNames @($compatibilityName) `
		-LegacyInternalCompatibilityRemovalBoundary $script:FlashCppLegacyInternalCompatibilityRemovalBoundary
	Assert-Runner ($compatibilityObjectResult.Status -eq "Bad") "legacy internal-failure compatibility still forbids object output"

	$compatibilityTimeoutResult = Test-FlashCppNegativeCompileResult -FileName $compatibilityName `
		-Started $true -TimedOut $true -ExitCode $null -ObjectExists $false `
		-CompilerOutput "" -SourceRejectionExit $script:FlashCppSourceRejectionExit `
		-InternalFailureExit $script:FlashCppInternalFailureExit -LegacyInternalCompatibilityNames @($compatibilityName) `
		-LegacyInternalCompatibilityRemovalBoundary $script:FlashCppLegacyInternalCompatibilityRemovalBoundary
	Assert-Runner ($compatibilityTimeoutResult.Status -eq "Bad" -and $compatibilityTimeoutResult.Detail -match "timed out") "legacy internal-failure compatibility cannot hide a compiler timeout"

	$cleanResult = Test-FlashCppNegativeCompileResult -FileName "test_invalid_e1001_e1001_e1051.cpp" `
		-Started $true -TimedOut $false -ExitCode $script:FlashCppSourceRejectionExit -ObjectExists $false `
		-CompilerOutput $diagnosticOutput -SourceRejectionExit $script:FlashCppSourceRejectionExit `
		-InternalFailureExit $script:FlashCppInternalFailureExit -LegacyInternalCompatibilityNames @() `
		-LegacyInternalCompatibilityRemovalBoundary $script:FlashCppLegacyInternalCompatibilityRemovalBoundary
	Assert-Runner ($cleanResult.Status -eq "Ok") "clean source rejection with the exact ID multiset passes"

	$nameValidation = Test-FlashCppNegativeNames -RepoRoot $repoRoot
	$inventoryValidation = Test-FlashCppLegacyNegativeInventory -RepoRoot $repoRoot -InventoryPath (Join-Path $repoRoot "tests\legacy_negative_tests.txt")
	$internalCompatibilityValidation = Test-FlashCppLegacyInternalCompatibility `
		-RepoRoot $repoRoot `
		-CompatibilityPath (Join-Path $repoRoot "tests\legacy_internal_failure_tests.txt") `
		-LegacyInventoryPath (Join-Path $repoRoot "tests\legacy_negative_tests.txt")
	Assert-Runner ($nameValidation.Valid -and $inventoryValidation.Valid) "the frozen legacy inventory matches exactly one current representation per entry"
	Assert-Runner ($internalCompatibilityValidation.Valid -and $internalCompatibilityValidation.ActiveCount -eq 7) "the seven-entry legacy internal-failure compatibility inventory is active at its baseline"

	$savedInternalCompatibilityBaseline = $script:FlashCppLegacyInternalCompatibilityBaseline
	$script:FlashCppLegacyInternalCompatibilityBaseline = 6
	$internalCompatibilityRegression = Test-FlashCppLegacyInternalCompatibility `
		-RepoRoot $repoRoot `
		-CompatibilityPath (Join-Path $repoRoot "tests\legacy_internal_failure_tests.txt") `
		-LegacyInventoryPath (Join-Path $repoRoot "tests\legacy_negative_tests.txt")
	$script:FlashCppLegacyInternalCompatibilityBaseline = $savedInternalCompatibilityBaseline
	Assert-Runner (-not $internalCompatibilityRegression.Valid -and
		$internalCompatibilityRegression.Error -match "above baseline 6") "the compatibility count cannot rise above its directional baseline"

	$inventoryRepo = Join-Path $tempRoot "inventory_repo"
	$inventoryTests = Join-Path $inventoryRepo "tests"
	New-Item -ItemType Directory -Path $inventoryTests -Force | Out-Null
	$inventoryCopy = Join-Path $inventoryRepo "legacy_negative_tests.txt"
	Copy-Item -LiteralPath (Join-Path $repoRoot "tests\legacy_negative_tests.txt") -Destination $inventoryCopy
	Set-Content -LiteralPath (Join-Path $inventoryTests "new_negative_fail.cpp") -Value "invalid"
	$unknownFail = Test-FlashCppLegacyNegativeInventory -RepoRoot $inventoryRepo -InventoryPath $inventoryCopy
	Assert-Runner (-not $unknownFail.Valid -and $unknownFail.Error -match "unregistered legacy negative test") "an unknown _fail.cpp name is rejected even if the inventory count would stay fixed"

	$mutatedInventory = Join-Path $tempRoot "mutated_inventory.txt"
	Copy-Item -LiteralPath (Join-Path $repoRoot "tests\legacy_negative_tests.txt") -Destination $mutatedInventory
	$mutatedInventoryBytes = [IO.File]::ReadAllBytes($mutatedInventory)
	$mutatedInventoryBytes[0] = ($mutatedInventoryBytes[0] -bxor 1)
	[IO.File]::WriteAllBytes($mutatedInventory, $mutatedInventoryBytes)
	$inventoryMutation = Test-FlashCppLegacyNegativeInventory -RepoRoot $repoRoot -InventoryPath $mutatedInventory
	Assert-Runner (-not $inventoryMutation.Valid -and $inventoryMutation.Error -match "SHA-256") "same-count inventory mutations fail the fixed SHA-256 guard"

	$mutatedInternalCompatibility = Join-Path $tempRoot "mutated_internal_compatibility.txt"
	Copy-Item -LiteralPath (Join-Path $repoRoot "tests\legacy_internal_failure_tests.txt") -Destination $mutatedInternalCompatibility
	$mutatedInternalCompatibilityBytes = [IO.File]::ReadAllBytes($mutatedInternalCompatibility)
	$mutatedInternalCompatibilityBytes[0] = ($mutatedInternalCompatibilityBytes[0] -bxor 1)
	[IO.File]::WriteAllBytes($mutatedInternalCompatibility, $mutatedInternalCompatibilityBytes)
	$internalCompatibilityMutation = Test-FlashCppLegacyInternalCompatibility `
		-RepoRoot $repoRoot `
		-CompatibilityPath $mutatedInternalCompatibility `
		-LegacyInventoryPath (Join-Path $repoRoot "tests\legacy_negative_tests.txt")
	Assert-Runner (-not $internalCompatibilityMutation.Valid -and $internalCompatibilityMutation.Error -match "SHA-256") "count-preserving compatibility inventory swaps fail the fixed SHA-256 guard"

	$compatibilityRepo = Join-Path $tempRoot "compatibility_repo"
	$compatibilityTests = Join-Path $compatibilityRepo "tests"
	New-Item -ItemType Directory -Path $compatibilityTests -Force | Out-Null
	$compatibilityLegacyInventory = Join-Path $compatibilityRepo "legacy_negative_tests.txt"
	$compatibilityInventory = Join-Path $compatibilityRepo "legacy_internal_failure_tests.txt"
	Copy-Item -LiteralPath (Join-Path $repoRoot "tests\legacy_negative_tests.txt") -Destination $compatibilityLegacyInventory
	Copy-Item -LiteralPath (Join-Path $repoRoot "tests\legacy_internal_failure_tests.txt") -Destination $compatibilityInventory
	$compatibilityEntries = @([IO.File]::ReadAllLines($compatibilityInventory, [Text.Encoding]::UTF8))
	foreach ($entry in $compatibilityEntries) {
		New-Item -ItemType File -Path (Join-Path $compatibilityTests $entry) -Force | Out-Null
	}
	$firstCompatibilityEntry = $compatibilityEntries[0]
	$firstCompatibilityStem = $firstCompatibilityEntry.Substring(0, $firstCompatibilityEntry.Length - "_fail.cpp".Length)
	Remove-Item -LiteralPath (Join-Path $compatibilityTests $firstCompatibilityEntry)
	New-Item -ItemType File -Path (Join-Path $compatibilityTests "${firstCompatibilityStem}_e1001.cpp") -Force | Out-Null
	$encodedSuccessorMapping = Test-FlashCppLegacyInternalCompatibility `
		-RepoRoot $compatibilityRepo `
		-CompatibilityPath $compatibilityInventory `
		-LegacyInventoryPath $compatibilityLegacyInventory
	Assert-Runner ($encodedSuccessorMapping.Valid -and
		$encodedSuccessorMapping.ActiveCount -eq 6 -and
		$encodedSuccessorMapping.ActiveNames -cnotcontains $firstCompatibilityEntry) "an encoded successor satisfies historical representation but loses the compatibility exception"

	Remove-Item -LiteralPath (Join-Path $compatibilityTests "${firstCompatibilityStem}_e1001.cpp")
	$missingCompatibilityRepresentation = Test-FlashCppLegacyInternalCompatibility `
		-RepoRoot $compatibilityRepo `
		-CompatibilityPath $compatibilityInventory `
		-LegacyInventoryPath $compatibilityLegacyInventory
	Assert-Runner (-not $missingCompatibilityRepresentation.Valid -and
		$missingCompatibilityRepresentation.Error -match "0 current representations") "a compatibility entry with no legacy or encoded representation is rejected"

	$missingLegacyMembership = Join-Path $compatibilityRepo "missing_legacy_membership.txt"
	$legacyMembershipLines = @(
		[IO.File]::ReadAllLines((Join-Path $repoRoot "tests\legacy_negative_tests.txt"), [Text.Encoding]::UTF8) |
			Where-Object { $_ -cne $firstCompatibilityEntry }
	)
	[IO.File]::WriteAllLines($missingLegacyMembership, $legacyMembershipLines, [Text.UTF8Encoding]::new($false))
	$compatibilityMembership = Test-FlashCppLegacyInternalCompatibility `
		-RepoRoot $repoRoot `
		-CompatibilityPath (Join-Path $repoRoot "tests\legacy_internal_failure_tests.txt") `
		-LegacyInventoryPath $missingLegacyMembership
	Assert-Runner (-not $compatibilityMembership.Valid -and
		$compatibilityMembership.Error -match "not in the frozen legacy inventory") "compatibility entries outside the frozen 259-name inventory are rejected"

	$manifestTests = Join-Path $tempRoot "manifest_tests"
	New-Item -ItemType Directory -Path $manifestTests -Force | Out-Null
	Set-Content -LiteralPath (Join-Path $manifestTests "positive_ret0.cpp") -Value "int main() { return 0; }"
	$manifestPath = Join-Path $tempRoot "expected_failures.tsv"
	[IO.File]::WriteAllText(
		$manifestPath,
		"test`tstage`tremoval_boundary`treason$([Environment]::NewLine)positive_ret0.cpp`tcompile`tboundary-3`tknown compiler defect$([Environment]::NewLine)",
		[Text.UTF8Encoding]::new($false))
	$manifest = Read-FlashCppExpectedFailures -ManifestPath $manifestPath -TestsRoot $manifestTests
	Assert-Runner ($manifest.Valid -and $manifest.Stages["positive_ret0.cpp"] -eq "compile") "a well-formed positive expected-failure manifest loads"

	$validSchedule = Test-FlashCppExpectedFailureSchedule -ExpectedFailures $manifest -ScheduledNames @("positive_ret0.cpp")
	Assert-Runner $validSchedule.Valid "a manifest entry for a scheduled regular test is accepted"
	$invalidSchedule = Test-FlashCppExpectedFailureSchedule -ExpectedFailures $manifest -ScheduledNames @()
	Assert-Runner (-not $invalidSchedule.Valid -and $invalidSchedule.Error -match "not a scheduled regular test") "manifest entries for excluded or support sources are rejected"

	[IO.File]::WriteAllText(
		$manifestPath,
		"test`tstage`tremoval_boundary`treason$([Environment]::NewLine)positive_ret0.cpp`tcompile`tboundary-3`tfirst$([Environment]::NewLine)positive_ret0.cpp`tlink`tboundary-4`tduplicate$([Environment]::NewLine)",
		[Text.UTF8Encoding]::new($false))
	$duplicateManifest = Read-FlashCppExpectedFailures -ManifestPath $manifestPath -TestsRoot $manifestTests
	Assert-Runner (-not $duplicateManifest.Valid -and $duplicateManifest.Error -match "duplicate") "duplicate expected-failure rows are rejected"

	[IO.File]::WriteAllText(
		$manifestPath,
		"test`tstage`tremoval_boundary`treason$([Environment]::NewLine)positive_ret0.cpp`tparse`tboundary-3`tbad stage$([Environment]::NewLine)",
		[Text.UTF8Encoding]::new($false))
	$badStageManifest = Read-FlashCppExpectedFailures -ManifestPath $manifestPath -TestsRoot $manifestTests
	Assert-Runner (-not $badStageManifest.Valid -and $badStageManifest.Error -match "invalid stage") "invalid expected-failure stages are rejected"

	[IO.File]::WriteAllText(
		$manifestPath,
		"test`tstage`tremoval_boundary`treason$([Environment]::NewLine)missing.cpp`tcompile`tboundary-3`tmissing file$([Environment]::NewLine)",
		[Text.UTF8Encoding]::new($false))
	$missingManifest = Read-FlashCppExpectedFailures -ManifestPath $manifestPath -TestsRoot $manifestTests
	Assert-Runner (-not $missingManifest.Valid -and $missingManifest.Error -match "does not exist") "expected-failure rows for missing tests are rejected"

	Set-Content -LiteralPath (Join-Path $manifestTests "negative_e1001.cpp") -Value "invalid"
	[IO.File]::WriteAllText(
		$manifestPath,
		"test`tstage`tremoval_boundary`treason$([Environment]::NewLine)negative_e1001.cpp`tcompile`tboundary-3`tnegative test$([Environment]::NewLine)",
		[Text.UTF8Encoding]::new($false))
	$negativeManifest = Read-FlashCppExpectedFailures -ManifestPath $manifestPath -TestsRoot $manifestTests
	Assert-Runner (-not $negativeManifest.Valid -and $negativeManifest.Error -match "negative test") "negative tests cannot enter the positive expected-failure manifest"

	[IO.File]::WriteAllText(
		$manifestPath,
		"test`tstage`tremoval_boundary`treason$([Environment]::NewLine)positive_ret0.cpp`tcompile`tboundary-3$([Environment]::NewLine)",
		[Text.UTF8Encoding]::new($false))
	$malformedManifest = Read-FlashCppExpectedFailures -ManifestPath $manifestPath -TestsRoot $manifestTests
	Assert-Runner (-not $malformedManifest.Valid -and $malformedManifest.Error -match "exactly four") "malformed expected-failure rows are rejected"

	Assert-Runner ((Compare-FlashCppExpectedStage -ExpectedStage "compile" -ActualStage "success").Result -eq "Stale") "expected failure followed by success is stale"
	Assert-Runner ((Compare-FlashCppExpectedStage -ExpectedStage "compile" -ActualStage "link").Result -eq "Stale") "a different terminal failure stage is stale"
	Assert-Runner ((Compare-FlashCppExpectedStage -ExpectedStage "compile" -ActualStage "compile").Result -eq "Expected") "the exact expected terminal stage matches"
} finally {
	Remove-Item -LiteralPath $tempRoot -Recurse -Force -ErrorAction SilentlyContinue
}

if ($failures -gt 0) {
	Write-Host "Runner self-tests failed: $failures" -ForegroundColor Red
	exit 1
}
Write-Host "Runner self-tests passed"
