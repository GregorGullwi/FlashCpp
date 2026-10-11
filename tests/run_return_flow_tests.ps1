$ErrorActionPreference = "Stop"

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoRoot = Split-Path -Parent $scriptDir
. (Join-Path $scriptDir "runner/RunnerCommon.ps1")
$compiler = Resolve-FlashCppCompilerPath -RepoRoot $repoRoot
if (-not $compiler) { throw "FlashCpp compiler not found" }
$outputRoot = Join-Path $repoRoot "x64/return-flow-diagnostics"
New-Item -ItemType Directory -Force -Path $outputRoot | Out-Null

# Runtime behavior is verified by run_all_tests.ps1. These contracts also assert
# warning absence, which a compile/link/run success cannot detect.
$cases = @(
	@{ File = "test_missing_return_warn_ret42.cpp"; Expected = @{ compute = 1 } },
	@{ File = "control_flow_comprehensive_ret130.cpp"; Expected = @{} },
	@{ File = "test_throw_through_inner_scope_dtor_ret0.cpp"; Expected = @{} },
	@{ File = "test_return_flow_ret0.cpp"; Expected = @{
		branch_missing = 1; switch_missing = 1; loop_missing = 1; break_missing = 1
		goto_bypasses_return = 1; goto_cycle_missing = 1; aggregate_missing = 1
		template_missing = 1; lambda_partial = 1; void_pointer_return_missing = 1
	} },
	@{ File = "test_return_flow_paths_ret0.cpp"; Expected = @{ "operator=" = 1; caught_missing = 1; main = 2 } },
	@{ File = "test_return_flow_call_metadata_ret0.cpp"; Expected = @{ caught_noreturn_missing = 1 } },
	@{ File = "test_return_flow_attribute_forms_ret0.cpp"; Expected = @{ ignored_attributes_can_return = 1 } },
	@{ File = "test_return_flow_switch_continue_ret0.cpp"; Expected = @{} }
)

foreach ($case in $cases) {
	$source = Join-Path $scriptDir $case.File
	$object = Join-Path $outputRoot ($case.File + ".obj")
	$compileOutput = & $compiler "--log-level=Codegen:warning" "-o" $object $source 2>&1 | Out-String
	if ($LASTEXITCODE -ne 0) { throw "FlashCpp failed for $($case.File)`n$compileOutput" }
	if (-not (Test-Path -LiteralPath $object)) { throw "FlashCpp did not produce $object`n$compileOutput" }
	$compileOutput | Set-Content -LiteralPath (Join-Path $outputRoot ($case.File + ".log"))
	if ($compileOutput -match "does not end with a return statement") { throw "Legacy return heuristic remains`n$compileOutput" }
	$warningIds = @([regex]::Matches($compileOutput, 'ControlReachesEndOfValueFunction#1620'))
	$warningMessages = @([regex]::Matches($compileOutput, "Control reaches end of non-void function '([^']+)'"))
	if ($warningIds.Count -ne $warningMessages.Count) { throw "Return-flow warnings lack their structured diagnostic id`n$compileOutput" }
	$actual = @{}
	foreach ($match in [regex]::Matches($compileOutput, "Control reaches end of non-void function '([^']+)'")) {
		$name = $match.Groups[1].Value
		$normalized = switch -Regex ($name) {
			'^template_missing(?:<.*)?$' { 'template_missing'; break }
			'lambda|operator\(\)' { 'lambda_partial'; break }
			default { $name }
		}
		if (-not $actual.ContainsKey($normalized)) { $actual[$normalized] = 0 }
		$actual[$normalized]++
	}
	$differences = @()
	$expected = $case.Expected
	foreach ($name in @($expected.Keys + $actual.Keys | Sort-Object -Unique)) {
		$expectedCount = if ($expected.ContainsKey($name)) { $expected[$name] } else { 0 }
		$actualCount = if ($actual.ContainsKey($name)) { $actual[$name] } else { 0 }
		if ($expectedCount -ne $actualCount) { $differences += "$name expected $expectedCount warning(s), got $actualCount" }
	}
	if ($differences.Count -gt 0) {
		throw "$($case.File): warning multiset mismatch:`n$($differences -join "`n")`n$compileOutput"
	}
	Write-Host "$($case.File): return-flow diagnostic contract passed"
}
