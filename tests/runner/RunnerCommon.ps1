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

function Get-FlashCppTestKind {
	param(
		[string]$FileName,
		[string]$SourceContent,
		[string[]]$PlatformExclusions,
		[string[]]$SupportSources,
		[string[]]$CompileOnlyOverrides
	)

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
