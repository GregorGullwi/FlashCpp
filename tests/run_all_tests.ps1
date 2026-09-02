# Reference Files Test Script for FlashCpp (PowerShell)
# This script compiles and links all .cpp files in tests/ and reports any failures
# Supports parallel execution with -Jobs N (default: number of CPU cores)

param(
	[Parameter(Position = 0, ValueFromRemainingArguments = $true)]
	[string[]]$TestFile = @(),
	[int]$Jobs = 0,
	[string]$CiOutput = "",
	[string]$MultiTuRoot = ""
)

$requestedTestNames = @(
	$TestFile |
		Where-Object { -not [string]::IsNullOrWhiteSpace($_) } |
		ForEach-Object { Split-Path $_ -Leaf }
)

# Default to number of logical processors
if ($Jobs -le 0) {
	$Jobs = [Environment]::ProcessorCount - 1
	if ($Jobs -le 0) { $Jobs = 4 }
}

# Suppress PowerShell errors from native commands writing to stderr
# (FlashCpp writes version info to stderr which is not an error)
$ErrorActionPreference = "SilentlyContinue"

# Navigate to the repository root (relative to this script's location)
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RepoRoot = Split-Path -Parent $ScriptDir
Set-Location $RepoRoot
. (Join-Path $ScriptDir "runner\RunnerCommon.ps1")

if (-not [string]::IsNullOrWhiteSpace($CiOutput) -and -not [System.IO.Path]::IsPathRooted($CiOutput)) {
	$CiOutput = Join-Path $RepoRoot $CiOutput
}
Initialize-FlashCppCiOutput -Path $CiOutput

Write-Host "=============================================="
Write-Host "FlashCpp Test Runner (PowerShell)"
Write-Host "=============================================="
Write-Host ""
Write-Host "Date: $(Get-Date)"
Write-Host ""

# Find the FlashCpp compiler executable
# On GitHub Actions, MSBuild builds FlashCppMSVC.exe
# Locally, build_flashcpp.bat builds FlashCpp.exe
# Linux make sharded builds x64/Sharded/FlashCpp
$flashCppPath = Resolve-FlashCppCompilerPath -RepoRoot $RepoRoot
if (-not $flashCppPath) {
	Write-Host "FlashCpp not found, building..."
	& .\build_flashcpp.bat
	if ($LASTEXITCODE -ne 0) {
		Write-Host "ERROR: Failed to build FlashCpp" -ForegroundColor Red
		Write-FlashCppCiRecord -Path $CiOutput -Kind "runner" -Name "compiler" -Status "build-failed" -Detail "build_flashcpp.bat failed"
		exit 1
	}
	$flashCppPath = Resolve-FlashCppCompilerPath -RepoRoot $RepoRoot
	if (-not $flashCppPath) {
		Write-Host "ERROR: FlashCpp compiler not found after build" -ForegroundColor Red
		Write-FlashCppCiRecord -Path $CiOutput -Kind "runner" -Name "compiler" -Status "missing-binary" -Detail "compiler executable not found after build"
		exit 1
	}
}

# Resolve to absolute path so parallel runspaces (which have a different working
# directory) can still invoke the compiler without a CommandNotFoundException.
$flashCppPath = (Get-Item -LiteralPath $flashCppPath).FullName

$freshness = Test-FlashCppBinaryFreshness -BinaryPath $flashCppPath -SourceFiles @(Get-FlashCppRelevantSourceFiles -RepoRoot $RepoRoot)
if (-not $freshness.IsFresh) {
	$message = "Compiler binary is older than $($freshness.NewestSource). Rebuild the compiler and retry."
	Write-Host "ERROR: $message" -ForegroundColor Red
	Write-FlashCppCiRecord -Path $CiOutput -Kind "runner" -Name "compiler" -Status "stale-binary" -Detail $message
	exit 1
}

# Get FlashCpp build info
$buildDate = (Get-Item $flashCppPath).LastWriteTime
Write-Host "Using: $flashCppPath"
Write-Host "Built: $buildDate"
Write-Host ""

# Find the linker (link.exe) from MSVC
$linkerPath = "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.44.35207\bin\Hostx64\x64\link.exe"
if (-not (Test-Path $linkerPath)) {
	# Try to find it dynamically
	$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
	if (Test-Path $vswhere) {
		$vsPath = & $vswhere -latest -property installationPath
		if ($vsPath) {
			$possibleLinker = Get-ChildItem -Path "$vsPath\VC\Tools\MSVC" -Recurse -Filter "link.exe" | 
				Where-Object { $_.FullName -match "Hostx64\\x64" } | 
				Select-Object -First 1
			if ($possibleLinker) {
				$linkerPath = $possibleLinker.FullName
			}
		}
	}
}

if (-not (Test-Path $linkerPath)) {
	Write-Host "ERROR: Could not find MSVC linker (link.exe)" -ForegroundColor Red
	Write-Host "Please ensure Visual Studio 2022 is installed" -ForegroundColor Red
	exit 1
}

Write-Host "Using linker: $linkerPath"
Write-Host ""

$cCompilerPath = Join-Path (Split-Path $linkerPath -Parent) "cl.exe"
if (-not (Test-Path $cCompilerPath)) {
	Write-Host "ERROR: Could not find MSVC C compiler (cl.exe) next to link.exe" -ForegroundColor Red
	exit 1
}

Write-Host "Using C compiler: $cCompilerPath"
Write-Host ""

# Get library paths
$vcToolsPath = Split-Path (Split-Path (Split-Path (Split-Path $linkerPath)))
$libPath1 = "$vcToolsPath\lib\x64"

# Find Windows SDK
$sdkPath = "C:\Program Files (x86)\Windows Kits\10"
$libPath2 = ""
$libPath3 = ""

if (Test-Path $sdkPath) {
	# Find the latest SDK version
	$sdkVersion = Get-ChildItem -Path "$sdkPath\Lib" -Directory | 
		Sort-Object Name -Descending | 
		Select-Object -First 1
	if ($sdkVersion) {
		$libPath2 = "$sdkPath\Lib\$($sdkVersion.Name)\um\x64"
		$libPath3 = "$sdkPath\Lib\$($sdkVersion.Name)\ucrt\x64"
	}
}

Write-Host "Library paths:"
Write-Host "  $libPath1"
if ($libPath2) { Write-Host "  $libPath2" }
if ($libPath3) { Write-Host "  $libPath3" }
Write-Host ""

$negativeNameValidation = Test-FlashCppNegativeNames -RepoRoot $RepoRoot
if (-not $negativeNameValidation.Valid) {
	Write-Host "ERROR: $($negativeNameValidation.Error)" -ForegroundColor Red
	Write-FlashCppCiRecord -Path $CiOutput -Kind "discovery" -Name "negative-names" -Status "invalid" -Detail $negativeNameValidation.Error
	exit 1
}
$expectedFailures = Read-FlashCppExpectedFailures -ManifestPath (Join-Path $ScriptDir "expected_failures.tsv") -TestsRoot $ScriptDir
if (-not $expectedFailures.Valid) {
	Write-Host "ERROR: $($expectedFailures.Error)" -ForegroundColor Red
	Write-FlashCppCiRecord -Path $CiOutput -Kind "discovery" -Name "expected-failures" -Status "invalid" -Detail $expectedFailures.Error
	exit 1
}

# Both runners discover the root tests/*.cpp suite. Nested fixture, future, and
# standard-header directories require their dedicated runners.
$allTestFiles = Get-ChildItem -Path "tests" -Filter "*.cpp" | Sort-Object Name

# Linux-specific test files that should not run on Windows
$linuxOnlyTests = @(
	"test_dwarf_cfi.cpp",			  # Uses Linux-specific DWARF/ELF headers
	"test_builtin_constant_p_ret42.cpp"  # Uses GCC/Clang __builtin_constant_p (not available in MSVC)
)

# Sources used as link support are discovered but are not standalone tests.
$supportSources = @(
	"linux_exception_stubs.cpp"
)

$compileOnlyOverrides = @()

# Classify every discovered source. Compile-only tests are intentionally run;
# this prevents a missing or unusually-spelled main from silently dropping a test.
$regularFiles = @()
$failFiles = @()
$excludedFiles = @()
foreach ($file in $allTestFiles) {
	$sourceContent = Get-Content $file.FullName -Raw
	$kind = Get-FlashCppTestKind -FileName $file.Name -SourceContent $sourceContent -PlatformExclusions $linuxOnlyTests -SupportSources $supportSources -CompileOnlyOverrides $compileOnlyOverrides
	switch ($kind) {
		"CompileFailure" { $failFiles += $file }
		"Runnable" { $regularFiles += $file }
		"CompileOnly" { $regularFiles += $file }
		"PlatformExcluded" { $excludedFiles += [pscustomobject]@{ File = $file; Reason = "Linux-only" } }
		"SupportSource" { $excludedFiles += [pscustomobject]@{ File = $file; Reason = "link support source" } }
		"MalformedNegative" {
			$message = "Malformed diagnostic filename: $($file.Name)"
			Write-Host "ERROR: $message" -ForegroundColor Red
			Write-FlashCppCiRecord -Path $CiOutput -Kind "discovery" -Name $file.Name -Status "invalid-negative-name" -Detail $message
			exit 1
		}
		default {
			$message = "Eligible test file was discovered but not classified: $($file.FullName)"
			Write-Host "ERROR: $message" -ForegroundColor Red
			Write-FlashCppCiRecord -Path $CiOutput -Kind "discovery" -Name $file.Name -Status "skipped" -Detail $message
			exit 1
		}
	}
}

$expectedFailureSchedule = Test-FlashCppExpectedFailureSchedule -ExpectedFailures $expectedFailures `
	-ScheduledNames @($regularFiles | ForEach-Object { $_.Name })
if (-not $expectedFailureSchedule.Valid) {
	Write-Host "ERROR: $($expectedFailureSchedule.Error)" -ForegroundColor Red
	Write-FlashCppCiRecord -Path $CiOutput -Kind "discovery" -Name "expected-failures" -Status "invalid" -Detail $expectedFailureSchedule.Error
	exit 1
}

$referenceFiles = $regularFiles
$totalFailFiles = $failFiles.Count

if ([string]::IsNullOrWhiteSpace($MultiTuRoot)) {
	$multiTuRoot = Join-Path $ScriptDir "multi_tu"
} elseif ([System.IO.Path]::IsPathRooted($MultiTuRoot)) {
	$multiTuRoot = $MultiTuRoot
} else {
	$multiTuRoot = Join-Path $RepoRoot $MultiTuRoot
}
$multiTuCases = @(Get-FlashCppMultiTuCases -Root $multiTuRoot)
$invalidMultiTuCases = @($multiTuCases | Where-Object { $null -ne $_.Error })
if ($invalidMultiTuCases.Count -gt 0) {
	foreach ($case in $invalidMultiTuCases) {
		Write-Host "ERROR: Invalid multi-TU case '$($case.Name)': $($case.Error)" -ForegroundColor Red
		Write-FlashCppCiRecord -Path $CiOutput -Kind "discovery" -Name $case.Name -Status "invalid-multi-tu" -Detail $case.Error
	}
	exit 1
}

# Filter to specific test files if provided
if ($requestedTestNames.Count -gt 0) {
	$referenceFiles = $referenceFiles | Where-Object { $requestedTestNames -ccontains $_.Name }
	$failFiles = $failFiles | Where-Object { $requestedTestNames -ccontains $_.Name }
	$multiTuCases = @($multiTuCases | Where-Object { $requestedTestNames -ccontains $_.Name })

	$matchedNames = @($referenceFiles.Name) + @($failFiles.Name) + @($multiTuCases.Name)
	$missingNames = @($requestedTestNames | Where-Object { $matchedNames -cnotcontains $_ } | Select-Object -Unique)
	if ($missingNames.Count -gt 0) {
		Write-Host "ERROR: Test file(s) not found in tests/: $($missingNames -join ', ')" -ForegroundColor Red
		foreach ($name in $missingNames) { Write-FlashCppCiRecord -Path $CiOutput -Kind "discovery" -Name $name -Status "not-found" -Detail "requested test was not scheduled" }
		exit 1
	}
}

$totalFiles = $referenceFiles.Count
$totalFailFiles = $failFiles.Count
Write-Host "Found $totalFiles runnable or compile-only test files in tests/ ($Jobs parallel jobs)"
Write-Host "Found $totalFailFiles negative compile tests"
Write-Host "Found $($multiTuCases.Count) multi-TU test cases"
if ($excludedFiles.Count -gt 0) {
	Write-Host "Explicitly excluded $($excludedFiles.Count) platform/support sources"
	foreach ($excluded in $excludedFiles) { Write-Host "  $($excluded.File.Name): $($excluded.Reason)" }
}
Write-Host ""

# Tests that require additional C helper objects for linking.
$extraCHelpers = @{
	"test_external_abi.cpp" = @("test_external_abi_helper.c")
	"test_external_abi_simple.cpp" = @("test_external_abi_simple_helper.c")
	"test_atomic_builtin_pointer_intrinsics_ret0.cpp" = @("test_atomic_builtin_pointer_intrinsics_helper.c")
	"test_extern_var_ret42.cpp" = @("test_extern_var_ret42_helper.c")
	"test_declspec_dllimport_var_ret42.cpp" = @("test_declspec_dllimport_var_ret42_helper.c")
}

# Pre-cache main() detection to avoid reading files twice
$mainFileCache = @{}
foreach ($file in $referenceFiles) {
	$sourceContent = Get-Content $file.FullName -Raw
	$mainFileCache[$file.Name] = $sourceContent -match '\b(?:int|void)\s+main\s*\('
}

# ──────────────────────────────────────────────────────
# Create temp directory for parallel result collection (in working directory)
# ──────────────────────────────────────────────────────
$resultDir = Join-Path $RepoRoot "test_results_$PID"
if (Test-Path $resultDir) { Remove-Item $resultDir -Recurse -Force }
New-Item -ItemType Directory -Path $resultDir -Force | Out-Null

# ──────────────────────────────────────────────────────
# Determine whether to run in parallel (PS 7+) or sequential
# ──────────────────────────────────────────────────────
$useParallel = ($PSVersionTable.PSVersion.Major -ge 7) -and ($Jobs -gt 1) -and ($requestedTestNames.Count -eq 0)

function Get-ResultDetail {
	param(
		[string[]]$Parts,
		[int]$StartIndex = 2
	)

	if ($Parts.Count -le $StartIndex) {
		return ""
	}

	return (($Parts[$StartIndex..($Parts.Count - 1)] -join '|').Trim())
}

function Write-DetailSnippet {
	param(
		[string]$Detail,
		[int]$MaxLines = 4
	)

	if ([string]::IsNullOrWhiteSpace($Detail)) {
		return
	}

	$detailLines = @($Detail -split '\r?\n' | Where-Object { $_.Trim() -ne "" })
	if ($detailLines.Count -eq 0) {
		return
	}

	$snippetLines = @($detailLines | Select-Object -First $MaxLines)
	foreach ($detailLine in $snippetLines) {
		Write-Host "  $detailLine" -ForegroundColor Yellow
	}

	if ($detailLines.Count -gt $MaxLines) {
		Write-Host "  ..." -ForegroundColor Yellow
	}
}

function Wait-ParallelResultJob {
	param(
		$Job,
		[string]$Label,
		[int]$TotalCount,
		[string]$ResultDir,
		[int]$InitialCompleted = 0,
		[int]$PollSeconds = 5
	)

	if (-not $Job) {
		return
	}

	$lastReportedCount = -1
	do {
		Wait-Job -Job $Job -Timeout $PollSeconds | Out-Null

		$completedCount = 0
		if (Test-Path $ResultDir) {
			$completedCount = @(
				Get-ChildItem -Path $ResultDir -Filter "*.result" -File -ErrorAction SilentlyContinue
			).Count - $InitialCompleted
			if ($completedCount -lt 0) { $completedCount = 0 }
		}

		if ($completedCount -gt $TotalCount) {
			$completedCount = $TotalCount
		}

		if ($completedCount -ne $lastReportedCount) {
			Write-Host "[Progress] ${Label}: $completedCount / $TotalCount completed..."
			$lastReportedCount = $completedCount
		}
	} while ($Job.State -eq "Running" -or $Job.State -eq "NotStarted")

	Wait-Job -Job $Job | Out-Null
	Receive-Job -Job $Job -ErrorAction SilentlyContinue | Out-Null
	Remove-Job -Job $Job -Force -ErrorAction SilentlyContinue | Out-Null

	if ($lastReportedCount -lt $TotalCount) {
		Write-Host "[Progress] ${Label}: $TotalCount / $TotalCount completed..."
	}
}

# ──────────────────────────────────────────────────────
# Worker function for testing a single regular file
# ──────────────────────────────────────────────────────
function Invoke-TestOneFile {
	param($filePath, $fileName, $baseName, $flashCppPath, $linkerPath, $cCompilerPath, $libPath1, $libPath2, $libPath3, $hasMain, $sourceRejectionExit, $internalFailureExit, $extraCHelpers, $repoRoot, $resultDir)

	$ErrorActionPreference = "SilentlyContinue"

	# Use result directory for artifacts to avoid polluting system temp
	$uniqueSuffix = [guid]::NewGuid().ToString('N')
	$objFile = Join-Path $resultDir "${baseName}_$uniqueSuffix.obj"
	$exeFile = Join-Path $resultDir "run_$uniqueSuffix.exe"
	$ilkFile = Join-Path $resultDir "${baseName}_$uniqueSuffix.ilk"
	$pdbFile = Join-Path $resultDir "${baseName}_$uniqueSuffix.pdb"
	$helperObjFiles = @()

	# Match the ELF runner: tests without a _ret<N> suffix must return zero.
	$expectedReturnValue = 0
	if ($fileName -match '_ret(\d+)\.cpp$') {
		$expectedReturnValue = [int]$matches[1]
	}

	# Fallback: if the worker dies unexpectedly the result file still gets written
	$resultLine = "WORKER_ERROR|$fileName|unknown worker failure"
	try {
		# Compile with FlashCpp; -o directs the object file to the unique temp path
		$flashCppArgs = @("--log-level=1", "-o", $objFile, $filePath)
		if ($fileName -match "^test_no_access_control_flag_ret100.*\.cpp$") {
			$flashCppArgs = @("-fno-access-control") + $flashCppArgs
		}

		$compilerResult = Invoke-FlashCppCompilerProcess -FilePath $flashCppPath -Arguments $flashCppArgs -TimeoutSeconds 120
		$compileOutput = $compilerResult.Output
		$compileExitCode = $compilerResult.ExitCode

		if (-not $compilerResult.Started) {
			$resultLine = "COMPILER_DRIVER_FAIL|$fileName|$compileOutput"
		} elseif ($compilerResult.TimedOut) {
			$resultLine = "COMPILER_TIMEOUT|$fileName|compiler timed out"
		} elseif ($compileExitCode -eq $internalFailureExit) {
			$resultLine = "COMPILER_INTERNAL|$fileName|compiler reported internal failure"
		} elseif ($compileExitCode -eq $sourceRejectionExit) {
			if (Test-Path $objFile) {
				$resultLine = "COMPILER_DRIVER_FAIL|$fileName|source rejection produced an object file"
			} else {
				$allLines = $compileOutput -split "`n" | Where-Object {
					$_.Trim() -ne "" -and
					$_ -notmatch "===== FLASHCPP VERSION" -and
					$_ -notmatch "(Compilation Timing|Phase.*Time|Percentage|---|TOTAL|\|)"
				}
				$errorLines = $allLines | Where-Object { $_ -match "\[ERROR\]|\[FATAL\]|error:" }
				$detail = if ($errorLines) { ($errorLines | Select-Object -Last 3) -join "`n" } else { ($allLines | Select-Object -Last 3) -join "`n" }
				$resultLine = "COMPILE_FAIL|$fileName|$detail"
			}
		} elseif ($compileExitCode -ne 0) {
			$resultLine = "COMPILER_DRIVER_FAIL|$fileName|compiler returned unexpected status $compileExitCode"
		} elseif (-not (Test-Path $objFile)) {
			$resultLine = "COMPILER_DRIVER_FAIL|$fileName|successful compiler status produced no object file"
		} else {
			if (-not $hasMain) {
				$resultLine = "COMPILE_LINK_OK|$fileName|0|no main"
			} else {
				if ($extraCHelpers.ContainsKey($fileName)) {
					foreach ($helperFileName in $extraCHelpers[$fileName]) {
						$helperBaseName = [System.IO.Path]::GetFileNameWithoutExtension($helperFileName)
						$helperSourcePath = Join-Path $repoRoot "tests\$helperFileName"
						$helperObjFile = Join-Path $resultDir "${helperBaseName}_$uniqueSuffix.obj"
						$helperCompileArgs = @(
							"/nologo",
							"/c",
							"/TC",
							"/Fo$helperObjFile",
							$helperSourcePath
						)
						$helperCompileOutput = & $cCompilerPath $helperCompileArgs 2>&1 | Out-String
						if ($LASTEXITCODE -ne 0 -or -not (Test-Path $helperObjFile)) {
							$helperErrors = ($helperCompileOutput -split "`n" | Where-Object { $_ -match "error" } | Select-Object -Last 5) -join "`n"
							if ([string]::IsNullOrWhiteSpace($helperErrors)) {
								$helperErrors = ($helperCompileOutput -split "`n" | Where-Object { $_.Trim() -ne "" } | Select-Object -Last 5) -join "`n"
							}
							$resultLine = "SUPPORT_COMPILE_FAIL|$fileName|Failed to compile helper $helperFileName`n$helperErrors"
							break
						}
						$helperObjFiles += $helperObjFile
					}
				}

				if ($resultLine.StartsWith("SUPPORT_COMPILE_FAIL|")) {
					return
				}

				# Link
				$linkArgs = @(
					"/LIBPATH:$libPath1",
					"/SUBSYSTEM:CONSOLE",
					"/OUT:$exeFile",
					"/PDB:$pdbFile",
					$objFile
				)
				if ($helperObjFiles.Count -gt 0) { $linkArgs += $helperObjFiles }
				$linkArgs += @(
					"kernel32.lib",
					"libucrt.lib",
					"legacy_stdio_definitions.lib"
				)
				if ($libPath2) { $linkArgs = @("/LIBPATH:$libPath2") + $linkArgs }
				if ($libPath3) { $linkArgs = @("/LIBPATH:$libPath3") + $linkArgs }

				$linkOutput = & $linkerPath $linkArgs 2>&1 | Out-String
				$linkExitCode = $LASTEXITCODE
				$windowsExceptionCodes = @(
					-1073741819, -1073740791, -1073741571, -1073740940, -1073741795,
					-529697949  # 0xE06D7363 = uncaught MSVC C++ exception
				)

				if ($windowsExceptionCodes -contains $linkExitCode) {
					$signal = if ($linkExitCode -lt 0) { $linkExitCode + 4294967296 } else { $linkExitCode }
					$resultLine = "LINKER_CRASH|$fileName|0x$($signal.ToString('X8'))"
				} elseif ($linkExitCode -eq 0 -and -not (Test-Path $exeFile)) {
					$resultLine = "LINKER_DRIVER_FAIL|$fileName|successful linker status produced no executable"
				} elseif ($linkExitCode -eq 0 -and (Test-Path $exeFile)) {
					$exePath = (Get-Item $exeFile).FullName
					$cmdArgs = '/d /c ""' + $exePath + '""'
					# Runtime timeout policy (mirrors runner_common.sh): generous
					# window for parallel-load inflation, one retry so a transient
					# host stall cannot fail an instant-return program.
					$runtimeAttempts = 0
					$runtimeTimedOut = $false
					while ($true) {
						$proc = New-Object System.Diagnostics.Process
						$proc.StartInfo = New-Object System.Diagnostics.ProcessStartInfo("cmd.exe", $cmdArgs)
						$proc.StartInfo.UseShellExecute = $false
						$proc.StartInfo.CreateNoWindow = $true
						$proc.StartInfo.WorkingDirectory = $repoRoot
						$proc.Start() | Out-Null
						if (-not $proc.WaitForExit(30000)) {
							$proc.Kill()
							$proc.WaitForExit()
							$runtimeAttempts += 1
							if ($runtimeAttempts -gt 1) {
								$runtimeTimedOut = $true
								break
							}
							continue
						}
						break
					}
					if ($runtimeTimedOut) {
						$resultLine = "RUNTIME_TIMEOUT|$fileName|process timed out after $runtimeAttempts attempts"
						$proc.Dispose()
						return
					}
					$returnValue = $proc.ExitCode
					$proc.Dispose()

					$isWindowsCrash = $windowsExceptionCodes -contains $returnValue

					if ($isWindowsCrash) {
						$signal = if ($returnValue -lt 0) { $returnValue + 4294967296 } else { $returnValue }
						$resultLine = "RUNTIME_CRASH|$fileName|0x$($signal.ToString('X8'))"
					} else {
						# Linux truncates exit codes to 8 bits (0-255); apply the same
						# truncation here so return-value checks are consistent across platforms.
						$returnValue = $returnValue -band 0xFF
						if ($returnValue -ne $expectedReturnValue) {
							$resultLine = "RETURN_MISMATCH|$fileName|$expectedReturnValue|$returnValue"
						} else {
							$resultLine = "RETURN_OK|$fileName|$returnValue|"
						}
					}
				} else {
					$errors = ($linkOutput -split "`n" | Where-Object { $_ -match "error" } | Select-Object -Last 5) -join "`n"
					$resultLine = "LINK_FAIL|$fileName|$errors"
				}
			}
		}
	} catch {
		$resultLine = "WORKER_ERROR|$fileName|$_"
		} finally {
		# Always clean up temp artifacts
			foreach ($f in @($objFile, $exeFile, $ilkFile, $pdbFile) + $helperObjFiles) {
			if (Test-Path $f) { Remove-Item $f -Force -ErrorAction SilentlyContinue }
		}
		Set-Content -Path (Join-Path $resultDir "$fileName.result") -Value $resultLine -NoNewline
	}
}

# ──────────────────────────────────────────────────────
# Worker function for testing a single negative file
# ──────────────────────────────────────────────────────
function Invoke-TestOneFailFile {
	param($filePath, $fileName, $baseName, $flashCppPath, $sourceRejectionExit, $internalFailureExit, $resultDir)

	$ErrorActionPreference = "SilentlyContinue"

	# Use result directory for artifacts to avoid polluting system temp
	$uniqueSuffix = [guid]::NewGuid().ToString('N')
	$objFile = Join-Path $resultDir "${baseName}_$uniqueSuffix.obj"
	$ilkFile = Join-Path $resultDir "${baseName}_$uniqueSuffix.ilk"
	$pdbFile = Join-Path $resultDir "${baseName}_$uniqueSuffix.pdb"

	# Fallback result in case the worker encounters a terminating error
	$resultLine = "FAIL_BAD|$fileName|unknown worker failure"
	try {
		$compilerResult = Invoke-FlashCppCompilerProcess -FilePath $flashCppPath -Arguments @("--log-level=1", "-o", $objFile, $filePath) -TimeoutSeconds 120
		$failOutput = $compilerResult.Output
		$negativeResult = Test-FlashCppNegativeCompileResult -FileName $fileName -Started $compilerResult.Started `
			-TimedOut $compilerResult.TimedOut -ExitCode $compilerResult.ExitCode -ObjectExists (Test-Path $objFile) `
			-CompilerOutput $failOutput -SourceRejectionExit $sourceRejectionExit -InternalFailureExit $internalFailureExit
		switch ($negativeResult.Status) {
			"Ok" { $resultLine = "FAIL_OK|$fileName|" }
			"DiagnosticMismatch" { $resultLine = "DIAG_MISMATCH|$fileName|$($negativeResult.Detail)" }
			default { $resultLine = "FAIL_BAD|$fileName|$($negativeResult.Detail)" }
		}
	} catch {
		$resultLine = "FAIL_BAD|$fileName|worker error: $_"
	} finally {
		# Always clean up temp artifacts
		foreach ($f in @($objFile, $ilkFile, $pdbFile)) {
			if (Test-Path $f) { Remove-Item $f -Force -ErrorAction SilentlyContinue }
		}
		Set-Content -Path (Join-Path $resultDir "$fileName.result") -Value $resultLine -NoNewline
	}
}

function Invoke-TestOneMultiTuCase {
	param($case, $flashCppPath, $linkerPath, $libPath1, $libPath2, $libPath3, $sourceRejectionExit, $internalFailureExit, $repoRoot, $resultDir)

	$ErrorActionPreference = "SilentlyContinue"
	$uniqueSuffix = [guid]::NewGuid().ToString('N')
	$objectFiles = @()
	$exeFile = Join-Path $resultDir "run_$uniqueSuffix.exe"
	$pdbFile = Join-Path $resultDir "$($case.Name)_$uniqueSuffix.pdb"
	$resultLine = "WORKER_ERROR|$($case.Name)|unknown worker failure"
	try {
		foreach ($source in $case.Sources) {
			$objFile = Join-Path $resultDir "$($source.BaseName)_$uniqueSuffix.obj"
			$compilerResult = Invoke-FlashCppCompilerProcess -FilePath $flashCppPath -Arguments @("--log-level=1", "-o", $objFile, $source.FullName) -TimeoutSeconds 120
			$compileOutput = $compilerResult.Output
			if (-not $compilerResult.Started) {
				$resultLine = "COMPILER_DRIVER_FAIL|$($case.Name)|$($source.Name): $compileOutput"
				break
			}
			if ($compilerResult.TimedOut) {
				$resultLine = "COMPILER_TIMEOUT|$($case.Name)|$($source.Name): compiler timed out"
				break
			}
			if ($compilerResult.ExitCode -eq $internalFailureExit) {
				$resultLine = "COMPILER_INTERNAL|$($case.Name)|$($source.Name): compiler reported internal failure"
				break
			}
			if ($compilerResult.ExitCode -eq $sourceRejectionExit -and -not (Test-Path -LiteralPath $objFile)) {
				$detail = ($compileOutput -split "`n" | Where-Object { $_.Trim() -ne "" } | Select-Object -Last 3) -join "`n"
				$resultLine = "COMPILE_FAIL|$($case.Name)|$($source.Name): $detail"
				break
			}
			if ($compilerResult.ExitCode -ne 0 -or -not (Test-Path -LiteralPath $objFile)) {
				$resultLine = "COMPILER_DRIVER_FAIL|$($case.Name)|$($source.Name): inconsistent compiler result (exit: $($compilerResult.ExitCode))"
				break
			}
			$objectFiles += $objFile
		}

		if ($objectFiles.Count -eq $case.Sources.Count) {
			$dumpbinPath = Join-Path (Split-Path $linkerPath -Parent) "dumpbin.exe"
			$expectStrongFile = Join-Path $case.Directory "expect_strong_defs.txt"
			if (Test-Path -LiteralPath $expectStrongFile) {
				$strongDefError = Test-FlashCppExpectStrongDefs -DumpbinPath $dumpbinPath -ObjectFiles $objectFiles -ExpectFile $expectStrongFile
				if ($strongDefError) {
					$resultLine = "OBJ_CHECK_FAIL|$($case.Name)|$strongDefError"
				}
			}
			$expectComdatFile = Join-Path $case.Directory "expect_comdat_defs.txt"
			if ($resultLine.StartsWith("WORKER_ERROR") -and (Test-Path -LiteralPath $expectComdatFile)) {
				$comdatDefError = Test-FlashCppExpectComdatDefs -DumpbinPath $dumpbinPath -ObjectFiles $objectFiles -ExpectFile $expectComdatFile
				if ($comdatDefError) {
					$resultLine = "OBJ_CHECK_FAIL|$($case.Name)|$comdatDefError"
				}
			}
		}

		if ($objectFiles.Count -eq $case.Sources.Count -and $resultLine.StartsWith("WORKER_ERROR")) {
			$linkArgs = @("/LIBPATH:$libPath1", "/SUBSYSTEM:CONSOLE", "/OUT:$exeFile", "/PDB:$pdbFile") + $objectFiles
			$linkArgs += @("kernel32.lib", "libucrt.lib", "legacy_stdio_definitions.lib")
			if ($libPath2) { $linkArgs = @("/LIBPATH:$libPath2") + $linkArgs }
			if ($libPath3) { $linkArgs = @("/LIBPATH:$libPath3") + $linkArgs }
			$linkOutput = & $linkerPath $linkArgs 2>&1 | Out-String
			$linkExitCode = $LASTEXITCODE
			$windowsExceptionCodes = @(-1073741819, -1073740791, -1073741571, -1073740940, -1073741795, -529697949)
			if ($windowsExceptionCodes -contains $linkExitCode) {
				$signal = if ($linkExitCode -lt 0) { $linkExitCode + 4294967296 } else { $linkExitCode }
				$resultLine = "LINKER_CRASH|$($case.Name)|0x$($signal.ToString('X8'))"
			} elseif ($linkExitCode -eq 0 -and -not (Test-Path -LiteralPath $exeFile)) {
				$resultLine = "LINKER_DRIVER_FAIL|$($case.Name)|successful linker status produced no executable"
			} elseif ($linkExitCode -ne 0 -or -not (Test-Path -LiteralPath $exeFile)) {
				$detail = ($linkOutput -split "`n" | Where-Object { $_.Trim() -ne "" } | Select-Object -Last 5) -join "`n"
				$resultLine = "LINK_FAIL|$($case.Name)|$detail"
			} else {
				# Runtime timeout policy (mirrors runner_common.sh): generous
				# window for parallel-load inflation, one retry so a transient
				# host stall cannot fail an instant-return program.
				$runtimeAttempts = 0
				$runtimeTimedOut = $false
				while ($true) {
					$process = Start-Process -FilePath $exeFile -WorkingDirectory $repoRoot -NoNewWindow -PassThru
					if (-not $process.WaitForExit(30000)) {
						$process.Kill()
						$process.WaitForExit()
						$runtimeAttempts += 1
						if ($runtimeAttempts -gt 1) {
							$runtimeTimedOut = $true
							break
						}
						continue
					}
					break
				}
				if ($runtimeTimedOut) {
					$resultLine = "RUNTIME_TIMEOUT|$($case.Name)|process timed out after $runtimeAttempts attempts"
					$process.Dispose()
					return
				}
				$returnValue = $process.ExitCode
				$process.Dispose()
				if ($windowsExceptionCodes -contains $returnValue) {
					$signal = if ($returnValue -lt 0) { $returnValue + 4294967296 } else { $returnValue }
					$resultLine = "RUNTIME_CRASH|$($case.Name)|0x$($signal.ToString('X8'))"
				} else {
					$returnValue = $returnValue -band 0xFF
					$expectedValue = Get-FlashCppExpectedReturnValue -Name $case.Name
					if ($returnValue -eq $expectedValue) {
						$resultLine = "RETURN_OK|$($case.Name)|$returnValue|multi-tu"
					} else {
						$resultLine = "RETURN_MISMATCH|$($case.Name)|$expectedValue|$returnValue"
					}
				}
			}
		}
	} catch {
		$resultLine = "WORKER_ERROR|$($case.Name)|$_"
	} finally {
		foreach ($artifact in @($objectFiles) + @($exeFile, $pdbFile)) {
			if ($artifact -and (Test-Path -LiteralPath $artifact)) { Remove-Item -LiteralPath $artifact -Force -ErrorAction SilentlyContinue }
		}
		Set-Content -LiteralPath (Join-Path $resultDir "$($case.Name).result") -Value $resultLine -NoNewline
	}
}

$invokeTestOneFileDefinition = ${function:Invoke-TestOneFile}.ToString()
	$invokeTestOneFailFileDefinition = ${function:Invoke-TestOneFailFile}.ToString()
	$invokeFlashCppCompilerProcessDefinition = ${function:Invoke-FlashCppCompilerProcess}.ToString()
	$getFlashCppNegativeNameInfoDefinition = ${function:Get-FlashCppNegativeNameInfo}.ToString()
	$getFlashCppPlainDiagnosticIdsDefinition = ${function:Get-FlashCppPlainDiagnosticIds}.ToString()
	$compareFlashCppDiagnosticIdMultisetsDefinition = ${function:Compare-FlashCppDiagnosticIdMultisets}.ToString()
	$testFlashCppNegativeCompileResultDefinition = ${function:Test-FlashCppNegativeCompileResult}.ToString()
$sourceRejectionExit = $script:FlashCppSourceRejectionExit
$internalFailureExit = $script:FlashCppInternalFailureExit

# ──────────────────────────────────────────────────────
# Run regular tests
# ──────────────────────────────────────────────────────
if ($useParallel) {
	Write-Host "Running $totalFiles tests with $Jobs parallel jobs (PowerShell $($PSVersionTable.PSVersion.Major))..."
	$regularParallelJob = $referenceFiles | ForEach-Object -ThrottleLimit $Jobs -Parallel {
		Set-Location $using:RepoRoot
		${function:Invoke-TestOneFile} = $using:invokeTestOneFileDefinition
		${function:Invoke-FlashCppCompilerProcess} = $using:invokeFlashCppCompilerProcessDefinition
		$file = $_
		$hasMain = ($using:mainFileCache)[$file.Name]
		Invoke-TestOneFile $file.FullName $file.Name $file.BaseName $using:flashCppPath $using:linkerPath $using:cCompilerPath $using:libPath1 $using:libPath2 $using:libPath3 $hasMain $using:sourceRejectionExit $using:internalFailureExit $using:extraCHelpers $using:RepoRoot $using:resultDir
	} -AsJob
	Wait-ParallelResultJob -Job $regularParallelJob -Label "Regular tests" -TotalCount $totalFiles -ResultDir $resultDir
} else {
	if ($Jobs -gt 1 -and $PSVersionTable.PSVersion.Major -lt 7 -and -not $TestFile) {
		Write-Host "NOTE: Parallel execution requires PowerShell 7+. Running sequentially." -ForegroundColor Yellow
		Write-Host "      Upgrade to PS 7+ and use -Jobs $Jobs for parallel testing." -ForegroundColor Yellow
	}
	$currentFile = 0
	foreach ($file in $referenceFiles) {
		$currentFile++
		Write-Host "[$currentFile/$totalFiles] Testing $($file.Name)... " -NoNewline
		$hasMain = $mainFileCache[$file.Name]
			Invoke-TestOneFile $file.FullName $file.Name $file.BaseName $flashCppPath $linkerPath $cCompilerPath $libPath1 $libPath2 $libPath3 $hasMain $sourceRejectionExit $internalFailureExit $extraCHelpers $RepoRoot $resultDir

		# Read and display result inline for sequential mode
		$resultFile = Join-Path $resultDir "$($file.Name).result"
		if (Test-Path $resultFile) {
			$line = Get-Content $resultFile -Raw
			$parts = $line -split '\|', 4
				$detail = Get-ResultDetail -Parts $parts
			switch ($parts[0]) {
				"RETURN_OK"             { Write-Host "OK (returned $($parts[2]))" }
				"COMPILE_LINK_OK"       { Write-Host "OK (no main - link skipped)" }
				"RETURN_MISMATCH"       { Write-Host "[RETURN MISMATCH] expected $($parts[2]) got $($parts[3])" -ForegroundColor Red }
				"RUNTIME_CRASH"         { Write-Host "[RUNTIME CRASH] $($parts[2])" -ForegroundColor Red }
				"LINK_FAIL"             {
					Write-Host "[LINK FAILED]" -ForegroundColor Red
						Write-DetailSnippet -Detail $detail
				}
				"COMPILE_FAIL"          {
					Write-Host "[COMPILE FAILED]" -ForegroundColor Red
						Write-DetailSnippet -Detail $detail
				}
				default                 { Write-Host "[$($parts[0])] $detail" -ForegroundColor Red }
			}
		}
	}
}

# ──────────────────────────────────────────────────────
# Run negative tests
# ──────────────────────────────────────────────────────
Write-Host ""
Write-Host "=============================================="
Write-Host "Testing negative compile tests"
Write-Host "=============================================="
Write-Host ""

if ($useParallel -and $failFiles.Count -gt 0) {
	$initialFailCompleted = if (Test-Path $resultDir) {
		@(Get-ChildItem -Path $resultDir -Filter "*.result" -File -ErrorAction SilentlyContinue).Count
	} else {
		0
	}
	$failParallelJob = $failFiles | ForEach-Object -ThrottleLimit $Jobs -Parallel {
		Set-Location $using:RepoRoot
		${function:Invoke-TestOneFailFile} = $using:invokeTestOneFailFileDefinition
		${function:Invoke-FlashCppCompilerProcess} = $using:invokeFlashCppCompilerProcessDefinition
		${function:Get-FlashCppNegativeNameInfo} = $using:getFlashCppNegativeNameInfoDefinition
		${function:Get-FlashCppPlainDiagnosticIds} = $using:getFlashCppPlainDiagnosticIdsDefinition
		${function:Compare-FlashCppDiagnosticIdMultisets} = $using:compareFlashCppDiagnosticIdMultisetsDefinition
		${function:Test-FlashCppNegativeCompileResult} = $using:testFlashCppNegativeCompileResultDefinition
		$file = $_
		Invoke-TestOneFailFile $file.FullName $file.Name $file.BaseName $using:flashCppPath $using:sourceRejectionExit $using:internalFailureExit $using:resultDir
	} -AsJob
	Wait-ParallelResultJob -Job $failParallelJob -Label "Negative tests" -TotalCount $failFiles.Count -ResultDir $resultDir -InitialCompleted $initialFailCompleted
} else {
	$currentFile = 0
	foreach ($file in $failFiles) {
		$currentFile++
		Write-Host "[$currentFile/$totalFailFiles] Testing $($file.Name)... " -NoNewline
		Invoke-TestOneFailFile $file.FullName $file.Name $file.BaseName $flashCppPath $sourceRejectionExit $internalFailureExit $resultDir

		$resultFile = Join-Path $resultDir "$($file.Name).result"
		if (Test-Path $resultFile) {
			$line = Get-Content $resultFile -Raw
			$parts = $line -split '\|', 3
			switch ($parts[0]) {
				"FAIL_OK"       { Write-Host "OK (failed as expected)" }
				"FAIL_BAD"      { Write-Host "[NEGATIVE CONTRACT FAILURE] $($parts[2])" -ForegroundColor Red }
				"DIAG_MISMATCH" {
					Write-Host "[DIAGNOSTIC MISMATCH] $($parts[2])" -ForegroundColor Red
				}
			}
		}
	}
}

# Multi-TU cases are few and run sequentially so their per-translation-unit
# compiler output stays grouped and actionable.
if ($multiTuCases.Count -gt 0) {
	Write-Host ""
	Write-Host "=============================================="
	Write-Host "Testing multi-translation-unit cases"
	Write-Host "=============================================="
	$currentCase = 0
	foreach ($case in $multiTuCases) {
		$currentCase++
		Write-Host "[$currentCase/$($multiTuCases.Count)] Testing $($case.Name)... " -NoNewline
		Invoke-TestOneMultiTuCase $case $flashCppPath $linkerPath $libPath1 $libPath2 $libPath3 $sourceRejectionExit $internalFailureExit $RepoRoot $resultDir
		$status = ((Get-Content -LiteralPath (Join-Path $resultDir "$($case.Name).result") -Raw) -split '\|', 2)[0]
		if ($status -eq "RETURN_OK") { Write-Host "OK" } else { Write-Host "[$status]" -ForegroundColor Red }
	}
}

# ──────────────────────────────────────────────────────
# Collect results from temp files
# ──────────────────────────────────────────────────────
$compileSuccess = @()
$compileFailed = @()
$linkSuccess = @()
$linkFailed = @()
$runSuccess = @()
$runFailed = @()
$runtimeCrashes = @()
$returnMismatches = @()
$failTestSuccess = @()
$failTestFailed = @()
$expectedFailureMatches = @()
$staleExpectations = @()
$staleExpectationDetails = @{}
$nonWaivableFailures = @()
$nonWaivableFailureDetails = @{}
$linkErrorDetails = @{}

foreach ($file in @($referenceFiles) + @($multiTuCases)) {
	$resultFile = Join-Path $resultDir "$($file.Name).result"
	if (-not (Test-Path $resultFile)) {
		$nonWaivableFailures += $file.Name
		$nonWaivableFailureDetails[$file.Name] = "missing worker result"
		continue
	}
	$line = Get-Content $resultFile -Raw
	$parts = $line -split '\|', 4
	$status = $parts[0]
		$detail = Get-ResultDetail -Parts $parts
	$expectedStage = if ($expectedFailures.Stages.ContainsKey($file.Name)) {
		$expectedFailures.Stages[$file.Name]
	} else {
		""
	}
	$actualStage = switch ($status) {
		"COMPILE_FAIL" { "compile"; break }
		"LINK_FAIL" { "link"; break }
		"RETURN_MISMATCH" { "run"; break }
		"RETURN_OK" { "success"; break }
		"COMPILE_LINK_OK" { "success"; break }
		default { $status.ToLowerInvariant(); break }
	}
	$expectation = Compare-FlashCppExpectedStage -ExpectedStage $expectedStage -ActualStage $actualStage
	if ($expectation.Result -eq "Expected") {
		$expectedFailureMatches += "$($file.Name) ($expectedStage)"
		switch ($status) {
			"LINK_FAIL" { $compileSuccess += $file.Name }
			"RETURN_MISMATCH" { $compileSuccess += $file.Name; $linkSuccess += $file.Name }
		}
		continue
	}
	if ($expectation.Result -eq "Stale") {
		switch ($status) {
			"RETURN_OK" { $compileSuccess += $file.Name; $linkSuccess += $file.Name }
			"COMPILE_LINK_OK" { $compileSuccess += $file.Name }
			"LINK_FAIL" { $compileSuccess += $file.Name }
			"RETURN_MISMATCH" { $compileSuccess += $file.Name; $linkSuccess += $file.Name }
		}
		$staleExpectations += $file.Name
		$staleExpectationDetails[$file.Name] = $expectation.Detail
		Write-Host "$($file.Name) - [STALE EXPECTATION] $($expectation.Detail)" -ForegroundColor Red
		continue
	}

	switch ($status) {
		"RETURN_OK" {
			$compileSuccess += $file.Name
			$linkSuccess += $file.Name
			$runSuccess += $file.Name
		}
		"RETURN_MISMATCH" {
			$compileSuccess += $file.Name
			$linkSuccess += $file.Name
			$returnMismatches += $file.Name
			if ($useParallel) {
				Write-Host "$($file.Name) - [RETURN MISMATCH] expected $($parts[2]) got $($parts[3])" -ForegroundColor Red
			}
		}
		"RUNTIME_CRASH" {
			$compileSuccess += $file.Name
			$linkSuccess += $file.Name
			$runtimeCrashes += $file.Name
			if ($useParallel) {
				Write-Host "$($file.Name) - [RUNTIME CRASH] $($parts[2])" -ForegroundColor Red
			}
		}
		"LINK_FAIL" {
			$compileSuccess += $file.Name
			$linkFailed += $file.Name
			if ($useParallel) {
				Write-Host "$($file.Name) - [LINK FAILED]" -ForegroundColor Red
					Write-DetailSnippet -Detail $detail
			}
			$linkErrorDetails[$file.Name] = @{
					Errors = @($detail)
				Unresolved = @()
					FullOutput = $detail
			}
		}
		"COMPILE_LINK_OK" {
			$compileSuccess += $file.Name
			$linkSuccess += $file.Name
		}
		"COMPILE_FAIL" {
			$compileFailed += $file.Name
			if ($useParallel) {
				Write-Host "$($file.Name) - [COMPILE FAILED]" -ForegroundColor Red
					Write-DetailSnippet -Detail $detail
			}
		}
		"COMPILER_TIMEOUT" {}
		"COMPILER_CRASH" {}
		"COMPILER_INTERNAL" {}
		"COMPILER_DRIVER_FAIL" {}
		"SUPPORT_COMPILE_FAIL" {}
		"LINKER_CRASH" {}
		"LINKER_DRIVER_FAIL" {}
		"RUNTIME_TIMEOUT" {}
		"WORKER_ERROR" {}
		"OBJ_CHECK_FAIL" {}
		default {
			$nonWaivableFailures += $file.Name
			$nonWaivableFailureDetails[$file.Name] = "$status`: $detail"
			Write-Host "$($file.Name) - [NON-WAIVABLE FAILURE] $status`: $detail" -ForegroundColor Red
		}
	}
	if ($status -in @(
		"COMPILER_TIMEOUT", "COMPILER_CRASH", "COMPILER_INTERNAL", "COMPILER_DRIVER_FAIL",
		"SUPPORT_COMPILE_FAIL", "LINKER_CRASH", "LINKER_DRIVER_FAIL", "RUNTIME_TIMEOUT",
		"WORKER_ERROR", "OBJ_CHECK_FAIL"
	)) {
		$nonWaivableFailures += $file.Name
		$nonWaivableFailureDetails[$file.Name] = "$status`: $detail"
		Write-Host "$($file.Name) - [NON-WAIVABLE FAILURE] $status`: $detail" -ForegroundColor Red
	}
}

foreach ($file in $failFiles) {
	$resultFile = Join-Path $resultDir "$($file.Name).result"
	if (-not (Test-Path $resultFile)) {
		$failTestFailed += "$($file.Name) (no result)"
		continue
	}
	$line = Get-Content $resultFile -Raw
	$parts = $line -split '\|', 3

	switch ($parts[0]) {
		"FAIL_OK"  { $failTestSuccess += $file.Name }
		"FAIL_BAD" {
			$failTestFailed += $file.Name
			if ($useParallel) {
				Write-Host "$($file.Name) - [NEGATIVE CONTRACT FAILURE] $($parts[2])" -ForegroundColor Red
			}
		}
		"DIAG_MISMATCH" {
			$failTestFailed += $file.Name
			if ($useParallel) {
				Write-Host "$($file.Name) - [DIAGNOSTIC MISMATCH] $($parts[2])" -ForegroundColor Red
			}
		}
	}
}

# Clean up temp directory
Remove-Item $resultDir -Recurse -Force -ErrorAction SilentlyContinue

# Summary
Write-Host ""
Write-Host "=============================================="
Write-Host "                   SUMMARY"
Write-Host "=============================================="
Write-Host ""
Write-Host "Total single-file tests: $totalFiles (with $Jobs parallel jobs)"
Write-Host "Total multi-TU cases: $($multiTuCases.Count)"
Write-Host ""
Write-Host "Regular Tests:"
Write-Host "  Compilation:"
Write-Host "    Success: $($compileSuccess.Count)" -ForegroundColor Green
Write-Host "    Failed:  $($compileFailed.Count)" -ForegroundColor Red
Write-Host ""
Write-Host "  Linking (of successfully compiled files):"
Write-Host "    Success: $($linkSuccess.Count)" -ForegroundColor Green
Write-Host "    Failed:  $($linkFailed.Count)" -ForegroundColor Red
Write-Host ""
Write-Host "  Runtime (of successfully linked files):"
$runtimePass = $runSuccess.Count
Write-Host "    Success: $runtimePass" -ForegroundColor Green
Write-Host "    Crashed: $($runtimeCrashes.Count)" -ForegroundColor Red
Write-Host "    Mismatches: $($returnMismatches.Count)" -ForegroundColor Red
Write-Host ""
Write-Host "Negative compile tests:"
Write-Host "  Failed as expected: $($failTestSuccess.Count)" -ForegroundColor Green
Write-Host "  Contract failures: $($failTestFailed.Count)" -ForegroundColor Red
Write-Host "  Legacy _fail.cpp classification: removed at boundary 2F" -ForegroundColor Green
Write-Host "  Expected positive failures matched: $($expectedFailureMatches.Count)" -ForegroundColor Green
Write-Host ""

if ($compileFailed.Count -gt 0) {
	Write-Host "=== Files that failed to compile ===" -ForegroundColor Red
	$compileFailed | Sort-Object | ForEach-Object {
		Write-Host "  - $_"
	}
	Write-Host ""
}

if ($linkFailed.Count -gt 0) {
	Write-Host "=== Files that failed to link ===" -ForegroundColor Red
	$linkFailed | Sort-Object | ForEach-Object {
		Write-Host "  - $_"
	}
	Write-Host ""
	
	# Show detailed link errors at the end for easy debugging
	Write-Host "=============================================="
	Write-Host "DETAILED LINK ERRORS (for easy debugging)"
	Write-Host "=============================================="
	Write-Host ""
	
	foreach ($fileName in ($linkFailed | Sort-Object)) {
		$errorInfo = $linkErrorDetails[$fileName]
		if ($errorInfo) {
			Write-Host "=== $fileName ===" -ForegroundColor Red
			
			if ($errorInfo.Errors.Count -gt 0) {
				Write-Host ""
				Write-Host "Link Errors:" -ForegroundColor Yellow
				foreach ($err in $errorInfo.Errors) {
					Write-Host "  $err"
				}
			}
			
			if ($errorInfo.Unresolved.Count -gt 0) {
				Write-Host ""
				Write-Host "Unresolved External Symbols:" -ForegroundColor Yellow
				foreach ($sym in $errorInfo.Unresolved) {
					Write-Host "  $sym"
				}
			}
			
			# Show a snippet of the full output if there are errors
			if ($errorInfo.Errors.Count -eq 0 -and $errorInfo.Unresolved.Count -eq 0) {
				Write-Host ""
				Write-Host "Full linker output:" -ForegroundColor Yellow
				$outputLines = $errorInfo.FullOutput -split "`n" | Where-Object { $_.Trim() -ne "" } | Select-Object -First 10
				foreach ($line in $outputLines) {
					Write-Host "  $line"
				}
			}
			
			Write-Host ""
		}
	}
}

if ($runtimeCrashes.Count -gt 0) {
	Write-Host "=== Files that crashed at runtime ===" -ForegroundColor Red
	$runtimeCrashes | Sort-Object | ForEach-Object {
		Write-Host "  - $_"
	}
	Write-Host ""
}

if ($returnMismatches.Count -gt 0) {
	Write-Host "=== Files with return value mismatches ===" -ForegroundColor Red
	$returnMismatches | Sort-Object | ForEach-Object {
		Write-Host "  - $_"
	}
	Write-Host ""
}

if ($staleExpectations.Count -gt 0) {
	Write-Host "=== Stale expected failures ===" -ForegroundColor Red
	foreach ($name in ($staleExpectations | Sort-Object)) {
		Write-Host "  - $name`: $($staleExpectationDetails[$name])"
	}
	Write-Host ""
}

if ($nonWaivableFailures.Count -gt 0) {
	Write-Host "=== Non-waivable runner/compiler failures ===" -ForegroundColor Red
	foreach ($name in ($nonWaivableFailures | Sort-Object)) {
		Write-Host "  - $name`: $($nonWaivableFailureDetails[$name])"
	}
	Write-Host ""
}

if ($failTestFailed.Count -gt 0) {
	Write-Host "=== Negative tests that failed their contract ===" -ForegroundColor Red
	$failTestFailed | Sort-Object | ForEach-Object {
		Write-Host "  - $_"
	}
	Write-Host ""
}

$exitCode = 0
$failureReasons = @()

if ($failTestFailed.Count -gt 0) {
	$failureReasons += "Some negative tests failed their diagnostic contract"
}
if ($compileFailed.Count -gt 0) {
	$failureReasons += "Some files did not compile successfully"
}
if ($linkFailed.Count -gt 0) {
	$failureReasons += "Some files did not link successfully"
}
if ($runtimeCrashes.Count -gt 0) {
	$failureReasons += "Some tests crashed at runtime"
}
if ($returnMismatches.Count -gt 0) {
	$failureReasons += "Some tests returned unexpected values"
}
if ($staleExpectations.Count -gt 0) {
	$failureReasons += "Some expected failures are stale"
}
if ($nonWaivableFailures.Count -gt 0) {
	$failureReasons += "Some tests had non-waivable runner or compiler failures"
}

if ($failureReasons.Count -gt 0) {
	$exitCode = 1
	Write-Host "RESULT: FAILED - $($failureReasons -join '; ')" -ForegroundColor Red
}
else {
	Write-Host "RESULT: SUCCESS - All files compiled and linked successfully!" -ForegroundColor Green
	Write-Host "                  All negative tests matched their contracts!" -ForegroundColor Green
	if ($runtimeCrashes.Count -eq 0 -and $returnMismatches.Count -eq 0) {
		Write-Host "                  All tests ran successfully!" -ForegroundColor Green
	}
}

foreach ($name in $compileFailed) { Write-FlashCppCiRecord -Path $CiOutput -Kind "test" -Name $name -Status "compile-failed" -Detail "" }
foreach ($name in $linkFailed) { Write-FlashCppCiRecord -Path $CiOutput -Kind "test" -Name $name -Status "link-failed" -Detail "" }
foreach ($name in $runtimeCrashes) { Write-FlashCppCiRecord -Path $CiOutput -Kind "test" -Name $name -Status "runtime-crash" -Detail "" }
foreach ($name in $returnMismatches) { Write-FlashCppCiRecord -Path $CiOutput -Kind "test" -Name $name -Status "return-mismatch" -Detail "" }
foreach ($name in $failTestFailed) { Write-FlashCppCiRecord -Path $CiOutput -Kind "test" -Name $name -Status "negative-contract-failed" -Detail "" }
foreach ($name in $staleExpectations) { Write-FlashCppCiRecord -Path $CiOutput -Kind "test" -Name $name -Status "stale-expectation" -Detail $staleExpectationDetails[$name] }
foreach ($name in $nonWaivableFailures) { Write-FlashCppCiRecord -Path $CiOutput -Kind "test" -Name $name -Status "non-waivable-failure" -Detail $nonWaivableFailureDetails[$name] }
Write-FlashCppCiRecord -Path $CiOutput -Kind "summary" -Name "all" -Status $(if ($exitCode -eq 0) { "success" } else { "failed" }) -Detail "single=$totalFiles multi-tu=$($multiTuCases.Count) failures=$($failureReasons.Count)"

Write-Host ""
Write-Host "=============================================="
exit $exitCode
