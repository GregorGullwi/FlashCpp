$script:FlashCppSourceRejectionExit = 1
$script:FlashCppInternalFailureExit = 2
$script:FlashCppLegacyInventoryCount = 36
$script:FlashCppLegacyInventorySha256 = "32b4ebb168414da1957bb83f02046d7aa25d5b6d227588e36538fb24e683a06b"
$script:FlashCppLegacyInternalCompatibilityCount = 5
$script:FlashCppLegacyInternalCompatibilitySha256 = "0dc664dec6df08f10b0dcf301d3f3cd6590dac95301ac9057a11a27c3fe23f0b"
$script:FlashCppLegacyInternalCompatibilityBaseline = 7
$script:FlashCppLegacyInternalCompatibilityRemovalBoundary = "2F"

function Get-FlashCppRelevantSourceFiles {
	param([string]$RepoRoot)

	$files = @(
		Get-ChildItem -LiteralPath (Join-Path $RepoRoot "src") -Recurse -File -ErrorAction SilentlyContinue |
			Where-Object { $_.Extension -in ".cpp", ".h", ".hpp" }
	)
	foreach ($relativePath in @("FlashCpp.vcxproj", "FlashCppMSVC.vcxproj", "Makefile", "build_flashcpp.bat")) {
		$path = Join-Path $RepoRoot $relativePath
		if (Test-Path -LiteralPath $path -PathType Leaf) {
			$files += Get-Item -LiteralPath $path
		}
	}
	return $files
}

function Test-FlashCppBinaryFreshness {
	param(
		[string]$BinaryPath,
		[System.IO.FileInfo[]]$SourceFiles
	)

	$binary = Get-Item -LiteralPath $BinaryPath
	$newestSource = $SourceFiles | Sort-Object LastWriteTimeUtc -Descending | Select-Object -First 1
	if ($null -eq $newestSource -or $binary.LastWriteTimeUtc -ge $newestSource.LastWriteTimeUtc) {
		return [pscustomobject]@{ IsFresh = $true; NewestSource = $null }
	}
	return [pscustomobject]@{ IsFresh = $false; NewestSource = $newestSource.FullName }
}

function Get-FlashCppNegativeNameInfo {
	param([string]$FileName)

	$result = [pscustomobject]@{
		Kind = "Other"
		Stem = ""
		ExpectedIds = @()
	}
	if (-not $FileName.EndsWith(".cpp", [StringComparison]::Ordinal)) {
		return $result
	}

	$work = $FileName.Substring(0, $FileName.Length - 4)
	$ids = [System.Collections.Generic.List[string]]::new()
	while ($true) {
		$match = [regex]::Match($work, '_e([1-9][0-9]*)$', [Text.RegularExpressions.RegexOptions]::CultureInvariant)
		if (-not $match.Success) { break }
		$ids.Insert(0, $match.Groups[1].Value)
		$work = $work.Substring(0, $match.Index)
	}
	if ($ids.Count -gt 0 -and $work.Length -gt 0) {
		if ([regex]::IsMatch($work, '_e[0-9]', [Text.RegularExpressions.RegexOptions]::CultureInvariant) -or
			[regex]::IsMatch($work, '_e[+-][0-9]', [Text.RegularExpressions.RegexOptions]::CultureInvariant) -or
			[regex]::IsMatch($work, '_e(_[A-Za-z0-9_]*)?$', [Text.RegularExpressions.RegexOptions]::CultureInvariant)) {
			$result.Kind = "Malformed"
			return $result
		}
		return [pscustomobject]@{
			Kind = "Encoded"
			Stem = $work
			ExpectedIds = @($ids)
		}
	}

	$withoutExtension = $FileName.Substring(0, $FileName.Length - 4)
	if ([regex]::IsMatch($withoutExtension, '_e[0-9]', [Text.RegularExpressions.RegexOptions]::CultureInvariant) -or
		[regex]::IsMatch($withoutExtension, '_e[+-][0-9]', [Text.RegularExpressions.RegexOptions]::CultureInvariant) -or
		[regex]::IsMatch($withoutExtension, '_e(_[A-Za-z0-9_]*)?$', [Text.RegularExpressions.RegexOptions]::CultureInvariant)) {
		$result.Kind = "Malformed"
	}
	return $result
}

function Get-FlashCppTestKind {
	param(
		[string]$FileName,
		[string]$SourceContent,
		[string[]]$PlatformExclusions,
		[string[]]$SupportSources,
		[string[]]$CompileOnlyOverrides
	)

	$negativeName = Get-FlashCppNegativeNameInfo -FileName $FileName
	if ($negativeName.Kind -eq "Malformed") { return "MalformedNegative" }
	if ($negativeName.Kind -eq "Encoded") { return "CompileFailure" }
	if ($PlatformExclusions -contains $FileName) { return "PlatformExcluded" }
	if ($SupportSources -contains $FileName) { return "SupportSource" }
	if ($CompileOnlyOverrides -contains $FileName) { return "CompileOnly" }
	if ($FileName -match '_fail\.cpp$') { return "CompileFailure" }
	if ($SourceContent -match '\b(?:int|void)\s+main\s*\(') { return "Runnable" }
	return "CompileOnly"
}

function Get-FlashCppExpectedReturnValue {
	param([string]$Name)

	if ($Name -match '_ret(\d+)(?:\.cpp)?$') {
		return [long]$matches[1]
	}
	return 0L
}

function Test-FlashCppLinuxReturnValue {
	param([string]$Name)

	$value = Get-FlashCppExpectedReturnValue -Name $Name
	return [pscustomobject]@{
		IsValid = $value -ge 0 -and $value -le 255
		Value = $value
	}
}

function Get-FlashCppMultiTuCases {
	param([string]$Root)

	if (-not (Test-Path -LiteralPath $Root -PathType Container)) { return @() }
	$cases = @()
	foreach ($directory in Get-ChildItem -LiteralPath $Root -Directory | Sort-Object Name) {
		$sources = @(Get-ChildItem -LiteralPath $directory.FullName -File -Filter "*.cpp" | Sort-Object Name)
		if ($sources.Count -eq 0) {
			$cases += [pscustomobject]@{ Name = $directory.Name; Directory = $directory.FullName; Sources = @(); Error = "no .cpp translation units" }
			continue
		}
		$mainCount = @($sources | Where-Object { (Get-Content -LiteralPath $_.FullName -Raw) -match '\b(?:int|void)\s+main\s*\(' }).Count
		$error = if ($mainCount -eq 1) { $null } else { "expected exactly one translation unit containing main, found $mainCount" }
		$cases += [pscustomobject]@{ Name = $directory.Name; Directory = $directory.FullName; Sources = $sources; Error = $error }
	}
	return $cases
}

function Initialize-FlashCppCiOutput {
	param([string]$Path)

	if ([string]::IsNullOrWhiteSpace($Path)) { return }
	$parent = Split-Path -Parent $Path
	if ($parent -and -not (Test-Path -LiteralPath $parent)) {
		New-Item -ItemType Directory -Path $parent -Force | Out-Null
	}
	[System.IO.File]::WriteAllText($Path, "flashcpp-runner-v1`tmeta`tschema`t1$([Environment]::NewLine)", [System.Text.UTF8Encoding]::new($false))
}

function Get-FlashCppOutsideEngineDiagnosticCount {
	param([string]$CompilerOutput)

	$match = [regex]::Match($CompilerOutput, 'Diagnostics emitted outside DiagnosticEngine:\s*(\d+)')
	if (-not $match.Success) { return $null }
	return [long]$match.Groups[1].Value
}

function Test-FlashCppMigrationCounterBaseline {
	param(
		[long]$ActualCount,
		[Nullable[long]]$BaselineCount
	)

	if ($null -eq $BaselineCount) { return "MissingBaseline" }
	if ($ActualCount -gt $BaselineCount) { return "Regressed" }
	if ($ActualCount -lt $BaselineCount) { return "Improved" }
	return "Ok"
}

function Get-FlashCppPlainDiagnosticIds {
	param([string]$CompilerOutput)

	$ids = @()
	foreach ($line in ($CompilerOutput -split "`r?`n")) {
		if ([string]::IsNullOrWhiteSpace($line)) { continue }
		$first = $line.TrimStart()[0]
		if ($first -eq [char]0x1b -or $first -eq '[') { continue }
		$match = [regex]::Match(
			$line,
			'^.*\[[A-Za-z][A-Za-z0-9_]*#(\d+)\]\s*$',
			[Text.RegularExpressions.RegexOptions]::CultureInvariant)
		if ($match.Success) {
			$ids += $match.Groups[1].Value
		}
	}
	return $ids
}

function Compare-FlashCppDiagnosticIdMultisets {
	param(
		[string[]]$Expected,
		[string[]]$Emitted
	)

	$expectedCounts = [System.Collections.Generic.Dictionary[string, int]]::new([StringComparer]::Ordinal)
	$emittedCounts = [System.Collections.Generic.Dictionary[string, int]]::new([StringComparer]::Ordinal)
	foreach ($id in @($Expected)) {
		if ($expectedCounts.ContainsKey($id)) {
			$expectedCounts[$id]++
		} else {
			$expectedCounts.Add($id, 1)
		}
	}
	foreach ($id in @($Emitted)) {
		if ($emittedCounts.ContainsKey($id)) {
			$emittedCounts[$id]++
		} else {
			$emittedCounts.Add($id, 1)
		}
	}
	$missing = @()
	$excess = @()
	foreach ($id in @($expectedCounts.Keys | Sort-Object)) {
		$actual = if ($emittedCounts.ContainsKey($id)) { $emittedCounts[$id] } else { 0 }
		if ($expectedCounts[$id] -gt $actual) {
			$missing += "$id x$($expectedCounts[$id] - $actual)"
		}
	}
	foreach ($id in @($emittedCounts.Keys | Sort-Object)) {
		$wanted = if ($expectedCounts.ContainsKey($id)) { $expectedCounts[$id] } else { 0 }
		if ($emittedCounts[$id] -gt $wanted) {
			$excess += "$id x$($emittedCounts[$id] - $wanted)"
		}
	}
	return [pscustomobject]@{
		Matched = ($missing.Count -eq 0 -and $excess.Count -eq 0)
		Missing = $missing
		Excess = $excess
	}
}

function Test-FlashCppNegativeCompileResult {
	param(
		[string]$FileName,
		[bool]$Started,
		[bool]$TimedOut,
		[Nullable[int]]$ExitCode,
		[bool]$ObjectExists,
		[string]$CompilerOutput,
		[int]$SourceRejectionExit,
		[int]$InternalFailureExit,
		[string[]]$LegacyInternalCompatibilityNames,
		[string]$LegacyInternalCompatibilityRemovalBoundary
	)

	if (-not $Started) {
		return [pscustomobject]@{ Status = "Bad"; Detail = "compiler could not start: $CompilerOutput" }
	}
	if ($TimedOut) {
		return [pscustomobject]@{ Status = "Bad"; Detail = "compiler timed out" }
	}
	if ($ExitCode -eq $InternalFailureExit) {
		$negativeName = Get-FlashCppNegativeNameInfo -FileName $FileName
		$isLegacyCompatibility = $FileName.EndsWith("_fail.cpp", [StringComparison]::Ordinal) -and
			$negativeName.Kind -eq "Other" -and
			$LegacyInternalCompatibilityNames -ccontains $FileName
		if ($isLegacyCompatibility) {
			if ($ObjectExists) {
				return [pscustomobject]@{ Status = "Bad"; Detail = "legacy internal-failure compatibility produced an object file" }
			}
			return [pscustomobject]@{
				Status = "LegacyInternalCompatibility"
				Detail = "temporary compatibility through boundary $LegacyInternalCompatibilityRemovalBoundary"
			}
		}
		return [pscustomobject]@{ Status = "Bad"; Detail = "compiler reported internal failure" }
	}
	if ($ExitCode -eq 0 -and $ObjectExists) {
		return [pscustomobject]@{ Status = "Bad"; Detail = "should have failed" }
	}
	if ($ExitCode -ne $SourceRejectionExit -or $ObjectExists) {
		return [pscustomobject]@{ Status = "Bad"; Detail = "inconsistent compiler result (exit: $ExitCode, object: $ObjectExists)" }
	}

	$negativeName = Get-FlashCppNegativeNameInfo -FileName $FileName
	if ($negativeName.Kind -eq "Encoded") {
		$emitted = @(Get-FlashCppPlainDiagnosticIds -CompilerOutput $CompilerOutput)
		$comparison = Compare-FlashCppDiagnosticIdMultisets -Expected $negativeName.ExpectedIds -Emitted $emitted
		if (-not $comparison.Matched) {
			$problems = @()
			foreach ($entry in $comparison.Missing) { $problems += "missing $entry" }
			foreach ($entry in $comparison.Excess) { $problems += "excess $entry" }
			return [pscustomobject]@{ Status = "DiagnosticMismatch"; Detail = $problems -join "; " }
		}
	}
	return [pscustomobject]@{ Status = "Ok"; Detail = "" }
}

function Test-FlashCppLegacyNegativeInventory {
	param(
		[string]$RepoRoot,
		[string]$InventoryPath
	)

	if (-not (Test-Path -LiteralPath $InventoryPath -PathType Leaf)) {
		return [pscustomobject]@{ Valid = $false; Error = "legacy inventory is missing: $InventoryPath" }
	}
	$inventory = @([IO.File]::ReadAllLines($InventoryPath, [Text.Encoding]::UTF8))
	if ($inventory.Count -ne $script:FlashCppLegacyInventoryCount) {
		return [pscustomobject]@{
			Valid = $false
			Error = "legacy inventory count is $($inventory.Count), expected $script:FlashCppLegacyInventoryCount"
		}
	}
	$hash = (Get-FileHash -LiteralPath $InventoryPath -Algorithm SHA256).Hash.ToLowerInvariant()
	if ($hash -cne $script:FlashCppLegacyInventorySha256) {
		return [pscustomobject]@{
			Valid = $false
			Error = "legacy inventory SHA-256 is $hash, expected $script:FlashCppLegacyInventorySha256"
		}
	}

	$inventorySet = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
	$previous = ""
	foreach ($name in $inventory) {
		if (-not $name.EndsWith("_fail.cpp", [StringComparison]::Ordinal)) {
			return [pscustomobject]@{ Valid = $false; Error = "invalid legacy inventory name: $name" }
		}
		if ($previous.Length -gt 0 -and [StringComparer]::Ordinal.Compare($previous, $name) -ge 0) {
			return [pscustomobject]@{ Valid = $false; Error = "legacy inventory is not strictly sorted at: $name" }
		}
		$null = $inventorySet.Add($name)
		$previous = $name
	}

	$testsRoot = Join-Path $RepoRoot "tests"
	foreach ($file in Get-ChildItem -LiteralPath $testsRoot -File -Filter "*_fail.cpp") {
		if (-not $inventorySet.Contains($file.Name)) {
			return [pscustomobject]@{ Valid = $false; Error = "unregistered legacy negative test: $($file.Name)" }
		}
	}

	foreach ($name in $inventory) {
		$original = Join-Path $testsRoot $name
		$stem = $name.Substring(0, $name.Length - "_fail.cpp".Length)
		$successors = @(
			Get-ChildItem -LiteralPath $testsRoot -File -Filter "${stem}_e*.cpp" |
				Where-Object {
					$info = Get-FlashCppNegativeNameInfo -FileName $_.Name
					$info.Kind -eq "Encoded" -and $info.Stem -ceq $stem
				}
		)
		if (Test-Path -LiteralPath $original -PathType Leaf) {
			if ($successors.Count -ne 0) {
				return [pscustomobject]@{ Valid = $false; Error = "legacy test and encoded successor both exist for: $name" }
			}
		} elseif ($successors.Count -ne 1) {
			return [pscustomobject]@{ Valid = $false; Error = "legacy inventory entry $name has $($successors.Count) encoded successors" }
		}
	}
	return [pscustomobject]@{ Valid = $true; Error = "" }
}

function Test-FlashCppLegacyInternalCompatibility {
	param(
		[string]$RepoRoot,
		[string]$CompatibilityPath,
		[string]$LegacyInventoryPath
	)

	$emptyResult = @{
		ActiveCount = 0
		ActiveNames = @()
		Baseline = $script:FlashCppLegacyInternalCompatibilityBaseline
		RemovalBoundary = $script:FlashCppLegacyInternalCompatibilityRemovalBoundary
	}
	if (-not (Test-Path -LiteralPath $CompatibilityPath -PathType Leaf)) {
		return [pscustomobject]($emptyResult + @{
			Valid = $false
			Error = "legacy internal-failure compatibility inventory is missing: $CompatibilityPath"
		})
	}
	if (-not (Test-Path -LiteralPath $LegacyInventoryPath -PathType Leaf)) {
		return [pscustomobject]($emptyResult + @{
			Valid = $false
			Error = "legacy negative inventory is missing: $LegacyInventoryPath"
		})
	}
	$compatibility = @([IO.File]::ReadAllLines($CompatibilityPath, [Text.Encoding]::UTF8))
	if ($compatibility.Count -ne $script:FlashCppLegacyInternalCompatibilityCount) {
		return [pscustomobject]($emptyResult + @{
			Valid = $false
			Error = "legacy internal-failure compatibility inventory count is $($compatibility.Count), expected $script:FlashCppLegacyInternalCompatibilityCount"
		})
	}
	$hash = (Get-FileHash -LiteralPath $CompatibilityPath -Algorithm SHA256).Hash.ToLowerInvariant()
	if ($hash -cne $script:FlashCppLegacyInternalCompatibilitySha256) {
		return [pscustomobject]($emptyResult + @{
			Valid = $false
			Error = "legacy internal-failure compatibility inventory SHA-256 is $hash, expected $script:FlashCppLegacyInternalCompatibilitySha256"
		})
	}

	$legacyInventory = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
	foreach ($name in [IO.File]::ReadAllLines($LegacyInventoryPath, [Text.Encoding]::UTF8)) {
		$null = $legacyInventory.Add($name)
	}
	$activeNames = [System.Collections.Generic.List[string]]::new()
	$previous = ""
	$testsRoot = Join-Path $RepoRoot "tests"
	foreach ($name in $compatibility) {
		if (-not $name.EndsWith("_fail.cpp", [StringComparison]::Ordinal)) {
			return [pscustomobject]($emptyResult + @{
				Valid = $false
				Error = "invalid legacy internal-failure compatibility name: $name"
			})
		}
		$negativeName = Get-FlashCppNegativeNameInfo -FileName $name
		if ($negativeName.Kind -ne "Other") {
			return [pscustomobject]($emptyResult + @{
				Valid = $false
				Error = "encoded or malformed name cannot enter legacy internal-failure compatibility: $name"
			})
		}
		if ($previous.Length -gt 0 -and [StringComparer]::Ordinal.Compare($previous, $name) -ge 0) {
			return [pscustomobject]($emptyResult + @{
				Valid = $false
				Error = "legacy internal-failure compatibility inventory is not strictly sorted at: $name"
			})
		}
		$previous = $name
		if (-not $legacyInventory.Contains($name)) {
			return [pscustomobject]($emptyResult + @{
				Valid = $false
				Error = "legacy internal-failure compatibility entry is not in the frozen legacy inventory: $name"
			})
		}

		$original = Join-Path $testsRoot $name
		$stem = $name.Substring(0, $name.Length - "_fail.cpp".Length)
		$successors = @(
			Get-ChildItem -LiteralPath $testsRoot -File -Filter "${stem}_e*.cpp" -ErrorAction SilentlyContinue |
				Where-Object {
					$info = Get-FlashCppNegativeNameInfo -FileName $_.Name
					$info.Kind -eq "Encoded" -and $info.Stem -ceq $stem
				}
		)
		if (Test-Path -LiteralPath $original -PathType Leaf) {
			if ($successors.Count -ne 0) {
				return [pscustomobject]($emptyResult + @{
					Valid = $false
					Error = "legacy internal-failure compatibility entry has both legacy and encoded representations: $name"
				})
			}
			$activeNames.Add($name)
		} elseif ($successors.Count -ne 1) {
			return [pscustomobject]($emptyResult + @{
				Valid = $false
				Error = "legacy internal-failure compatibility entry $name has $($successors.Count) current representations"
			})
		}
	}
	if ($activeNames.Count -gt $script:FlashCppLegacyInternalCompatibilityBaseline) {
		return [pscustomobject]($emptyResult + @{
			Valid = $false
			Error = "legacy internal-failure compatibility count is $($activeNames.Count), above baseline $script:FlashCppLegacyInternalCompatibilityBaseline"
		})
	}
	return [pscustomobject]@{
		Valid = $true
		Error = ""
		ActiveCount = $activeNames.Count
		ActiveNames = @($activeNames)
		Baseline = $script:FlashCppLegacyInternalCompatibilityBaseline
		RemovalBoundary = $script:FlashCppLegacyInternalCompatibilityRemovalBoundary
	}
}

function Test-FlashCppNegativeNames {
	param([string]$RepoRoot)

	foreach ($file in Get-ChildItem -LiteralPath (Join-Path $RepoRoot "tests") -File -Filter "*.cpp") {
		$info = Get-FlashCppNegativeNameInfo -FileName $file.Name
		if ($info.Kind -eq "Malformed") {
			return [pscustomobject]@{ Valid = $false; Error = "malformed diagnostic filename: $($file.Name)" }
		}
	}
	return [pscustomobject]@{ Valid = $true; Error = "" }
}

function Read-FlashCppExpectedFailures {
	param(
		[string]$ManifestPath,
		[string]$TestsRoot
	)

	$stages = [System.Collections.Generic.Dictionary[string, string]]::new([StringComparer]::Ordinal)
	$boundaries = [System.Collections.Generic.Dictionary[string, string]]::new([StringComparer]::Ordinal)
	$reasons = [System.Collections.Generic.Dictionary[string, string]]::new([StringComparer]::Ordinal)
	if (-not (Test-Path -LiteralPath $ManifestPath -PathType Leaf)) {
		return [pscustomobject]@{ Valid = $false; Error = "expected-failure manifest is missing: $ManifestPath"; Stages = $stages; Boundaries = $boundaries; Reasons = $reasons }
	}
	$lines = @([IO.File]::ReadAllLines($ManifestPath, [Text.Encoding]::UTF8))
	if ($lines.Count -eq 0 -or $lines[0] -cne "test`tstage`tremoval_boundary`treason") {
		return [pscustomobject]@{ Valid = $false; Error = "invalid expected-failure header"; Stages = $stages; Boundaries = $boundaries; Reasons = $reasons }
	}
	for ($lineIndex = 1; $lineIndex -lt $lines.Count; $lineIndex++) {
		$fields = $lines[$lineIndex].Split(@([char]"`t"), [StringSplitOptions]::None)
		$row = $lineIndex + 1
		if ($fields.Count -ne 4) {
			return [pscustomobject]@{ Valid = $false; Error = "expected-failure row $row must contain exactly four tab-separated fields"; Stages = $stages; Boundaries = $boundaries; Reasons = $reasons }
		}
		$name, $stage, $boundary, $reason = $fields
		if ($name.Length -eq 0 -or $boundary.Length -eq 0 -or $reason.Length -eq 0) {
			return [pscustomobject]@{ Valid = $false; Error = "expected-failure row $row has an empty required field"; Stages = $stages; Boundaries = $boundaries; Reasons = $reasons }
		}
		if ($stage -cnotin @("compile", "link", "run")) {
			return [pscustomobject]@{ Valid = $false; Error = "expected-failure row $row has invalid stage: $stage"; Stages = $stages; Boundaries = $boundaries; Reasons = $reasons }
		}
		if (-not [regex]::IsMatch($name, '^[A-Za-z0-9_.+-]+\.cpp$', [Text.RegularExpressions.RegexOptions]::CultureInvariant)) {
			return [pscustomobject]@{ Valid = $false; Error = "invalid expected-failure test name: $name"; Stages = $stages; Boundaries = $boundaries; Reasons = $reasons }
		}
		$negativeName = Get-FlashCppNegativeNameInfo -FileName $name
		if ($name.EndsWith("_fail.cpp", [StringComparison]::Ordinal) -or $negativeName.Kind -ne "Other") {
			return [pscustomobject]@{ Valid = $false; Error = "negative test cannot enter expected-failure manifest: $name"; Stages = $stages; Boundaries = $boundaries; Reasons = $reasons }
		}
		if (-not (Test-Path -LiteralPath (Join-Path $TestsRoot $name) -PathType Leaf)) {
			return [pscustomobject]@{ Valid = $false; Error = "expected-failure test does not exist: $name"; Stages = $stages; Boundaries = $boundaries; Reasons = $reasons }
		}
		if ($stages.ContainsKey($name)) {
			return [pscustomobject]@{ Valid = $false; Error = "duplicate expected-failure test: $name"; Stages = $stages; Boundaries = $boundaries; Reasons = $reasons }
		}
		$stages.Add($name, $stage)
		$boundaries.Add($name, $boundary)
		$reasons.Add($name, $reason)
	}
	return [pscustomobject]@{ Valid = $true; Error = ""; Stages = $stages; Boundaries = $boundaries; Reasons = $reasons }
}

function Test-FlashCppExpectedFailureSchedule {
	param(
		$ExpectedFailures,
		[string[]]$ScheduledNames
	)

	foreach ($name in $ExpectedFailures.Stages.Keys) {
		if ($ScheduledNames -cnotcontains $name) {
			return [pscustomobject]@{
				Valid = $false
				Error = "expected-failure test is not a scheduled regular test: $name"
			}
		}
	}
	return [pscustomobject]@{ Valid = $true; Error = "" }
}

function Compare-FlashCppExpectedStage {
	param(
		[string]$ExpectedStage,
		[string]$ActualStage
	)

	if ([string]::IsNullOrEmpty($ExpectedStage)) {
		return [pscustomobject]@{ Result = "None"; Detail = "" }
	}
	if ($ActualStage -cin @("compile", "link", "run")) {
		if ($ActualStage -ceq $ExpectedStage) {
			return [pscustomobject]@{ Result = "Expected"; Detail = "" }
		}
		return [pscustomobject]@{ Result = "Stale"; Detail = "expected $ExpectedStage failure, observed $ActualStage failure" }
	}
	if ($ActualStage -ceq "success") {
		return [pscustomobject]@{ Result = "Stale"; Detail = "expected $ExpectedStage failure, observed success" }
	}
	return [pscustomobject]@{ Result = "NonWaivable"; Detail = "expected $ExpectedStage failure, observed non-waivable $ActualStage" }
}

function Invoke-FlashCppCompilerProcess {
	param(
		[string]$FilePath,
		[string[]]$Arguments,
		[int]$TimeoutSeconds
	)

	$quoteArgument = {
		param([string]$Argument)

		if ($Argument.Length -gt 0 -and $Argument -notmatch '[\s"]') {
			return $Argument
		}

		$quoted = [Text.StringBuilder]::new()
		[void]$quoted.Append('"')
		$backslashCount = 0
		foreach ($character in $Argument.ToCharArray()) {
			if ($character -eq '\') {
				$backslashCount++
				continue
			}
			if ($character -eq '"') {
				[void]$quoted.Append([char]'\', ($backslashCount * 2) + 1)
				[void]$quoted.Append('"')
				$backslashCount = 0
				continue
			}
			if ($backslashCount -gt 0) {
				[void]$quoted.Append([char]'\', $backslashCount)
				$backslashCount = 0
			}
			[void]$quoted.Append($character)
		}
		if ($backslashCount -gt 0) {
			[void]$quoted.Append([char]'\', $backslashCount * 2)
		}
		[void]$quoted.Append('"')
		return $quoted.ToString()
	}

	$startInfo = [Diagnostics.ProcessStartInfo]::new()
	$startInfo.FileName = $FilePath
	$startInfo.UseShellExecute = $false
	$startInfo.CreateNoWindow = $true
	$startInfo.RedirectStandardOutput = $true
	$startInfo.RedirectStandardError = $true
	$startInfo.Arguments = (($Arguments | ForEach-Object { & $quoteArgument $_ }) -join ' ')

	$process = [Diagnostics.Process]::new()
	$process.StartInfo = $startInfo
	try {
		if (-not $process.Start()) {
			return [pscustomobject]@{ Started = $false; TimedOut = $false; ExitCode = $null; Output = "compiler process did not start" }
		}
		$stdoutTask = $process.StandardOutput.ReadToEndAsync()
		$stderrTask = $process.StandardError.ReadToEndAsync()
		if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
			$process.Kill()
			$process.WaitForExit()
			return [pscustomobject]@{
				Started = $true
				TimedOut = $true
				ExitCode = $process.ExitCode
				Output = $stdoutTask.Result + [Environment]::NewLine + $stderrTask.Result
			}
		}
		$process.WaitForExit()
		return [pscustomobject]@{
			Started = $true
			TimedOut = $false
			ExitCode = $process.ExitCode
			Output = $stdoutTask.Result + [Environment]::NewLine + $stderrTask.Result
		}
	} catch {
		return [pscustomobject]@{ Started = $false; TimedOut = $false; ExitCode = $null; Output = [string]$_ }
	} finally {
		$process.Dispose()
	}
}

function Write-FlashCppCiRecord {
	param(
		[string]$Path,
		[string]$Kind,
		[string]$Name,
		[string]$Status,
		[string]$Detail
	)

	$fields = @($Kind, $Name, $Status, $Detail) | ForEach-Object { ([string]$_) -replace "[`t`r`n]", " " }
	$record = "flashcpp-runner-v1`t" + ($fields -join "`t")
	if ([string]::IsNullOrWhiteSpace($Path)) {
		Write-Host $record
	} else {
		[System.IO.File]::AppendAllText($Path, "$record$([Environment]::NewLine)", [System.Text.UTF8Encoding]::new($false))
	}
}
