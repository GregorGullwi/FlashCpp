# Reference Files Test Script for FlashCpp (PowerShell)
# This script compiles and links all .cpp files in tests/ and reports any failures
# Supports parallel execution with -Jobs N (default: number of CPU cores)
# OPTIMIZED VERSION - uses call operator instead of Start-Process for better performance

param(
	[string]$TestFile = $null,
	[int]$Jobs = 0
)

# Default to number of logical processors
if ($Jobs -le 0) {
	$Jobs = [Environment]::ProcessorCount
	if ($Jobs -le 0) { $Jobs = 4 }
}

# Suppress PowerShell errors from native commands writing to stderr
# (FlashCpp writes version info to stderr which is not an error)
$ErrorActionPreference = "SilentlyContinue"

# Navigate to the repository root (relative to this script's location)
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RepoRoot = Split-Path -Parent $ScriptDir
Set-Location $RepoRoot

Write-Host "=============================================="
Write-Host "FlashCpp Test Runner (PowerShell)"
Write-Host "=============================================="
Write-Host ""
Write-Host "Date: $(Get-Date)"
Write-Host ""

# Find the FlashCpp compiler executable
# On GitHub Actions, MSBuild builds FlashCppMSVC.exe
# Locally, build_flashcpp.bat builds FlashCpp.exe
$flashCppPath = ""
if (Test-Path "x64\Debug\FlashCpp.exe") {
	$flashCppPath = "x64\Debug\FlashCpp.exe"
} elseif (Test-Path "x64\Debug\FlashCppMSVC.exe") {
	$flashCppPath = "x64\Debug\FlashCppMSVC.exe"
} else {
	Write-Host "FlashCpp not found, building..."
	& .\build_flashcpp.bat
	if ($LASTEXITCODE -ne 0) {
		Write-Host "ERROR: Failed to build FlashCpp" -ForegroundColor Red
		exit 1
	}
	if (Test-Path "x64\Debug\FlashCpp.exe") {
		$flashCppPath = "x64\Debug\FlashCpp.exe"
	} else {
		Write-Host "ERROR: FlashCpp.exe not found after build" -ForegroundColor Red
		exit 1
	}
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

# Get all .cpp files from tests/
$allTestFiles = Get-ChildItem -Path "tests" -Filter "*.cpp" | Sort-Object Name

# Linux-specific test files that should not run on Windows
$linuxOnlyTests = @(
	"test_dwarf_cfi.cpp",			  # Uses Linux-specific DWARF/ELF headers
	"test_builtin_constant_p_ret42.cpp"  # Uses GCC/Clang __builtin_constant_p (not available in MSVC)
)

# Expected runtime crashes - files that compile and link but crash at runtime
$expectedRuntimeCrashes = @(
)

# Filter to only files that have a main function and separate _fail files
$filesWithMain = @()
$failFiles = @()
foreach ($file in $allTestFiles) {
	# Skip Linux-only test files on Windows
	if ($linuxOnlyTests -contains $file.Name) {
		continue
	}
	
	$sourceContent = Get-Content $file.FullName -Raw
	$hasMain = $sourceContent -match '\bint\s+main\s*\(' -or $sourceContent -match '\bvoid\s+main\s*\('
	if ($hasMain) {
		if ($file.Name -match "_fail\.cpp$") {
			$failFiles += $file
		} else {
			$filesWithMain += $file
		}
	}
}

$referenceFiles = $filesWithMain
$totalFailFiles = $failFiles.Count

# Filter to specific test file if provided
if ($TestFile) {
	$referenceFiles = $referenceFiles | Where-Object { $_.Name -eq $TestFile }
	$failFiles = $failFiles | Where-Object { $_.Name -eq $TestFile }
	if ($referenceFiles.Count -eq 0 -and $failFiles.Count -eq 0) {
		Write-Host "ERROR: Test file '$TestFile' not found in tests/" -ForegroundColor Red
		exit 1
	}
}

$totalFiles = $referenceFiles.Count
$totalFailFiles = $failFiles.Count
Write-Host "Found $totalFiles test files with main() in tests/ ($Jobs parallel jobs)"
Write-Host "Found $totalFailFiles _fail test files (expected to fail compilation)"
Write-Host ""

# Expected compile failures - files that are intentionally designed to fail compilation
# or use features not yet implemented in FlashCpp
# NOTE: Files with _fail.cpp suffix are automatically tested separately
#
# Intentionally invalid code (tests error detection):
#
# Unimplemented features:
$expectedCompileFailures = @(
	"test_cstddef.cpp"
	"test_cstdio_puts.cpp"
	"test_cstdlib.cpp"
)

# Expected link failures - files that compile but have known link issues
# These are typically due to features not yet implemented in FlashCpp
$expectedLinkFailures = @(
	# ABI tests that require external C helper files to be compiled and linked
	"test_external_abi.cpp"
	"test_external_abi_simple.cpp"
)

# Pre-cache main() detection to avoid reading files twice
$mainFileCache = @{}
foreach ($file in $referenceFiles) {
	$sourceContent = Get-Content $file.FullName -Raw
	$mainFileCache[$file.Name] = $sourceContent -match '\bint\s+main\s*\(' -or $sourceContent -match '\bvoid\s+main\s*\('
}

# ──────────────────────────────────────────────────────
# Create temp directory for parallel result collection
# ──────────────────────────────────────────────────────
$resultDir = Join-Path ([System.IO.Path]::GetTempPath()) "flashcpp_test_results_$PID"
if (Test-Path $resultDir) { Remove-Item $resultDir -Recurse -Force }
New-Item -ItemType Directory -Path $resultDir -Force | Out-Null

# ──────────────────────────────────────────────────────
# Determine whether to run in parallel (PS 7+) or sequential
# ──────────────────────────────────────────────────────
$useParallel = ($PSVersionTable.PSVersion.Major -ge 7) -and ($Jobs -gt 1) -and (-not $TestFile)

# ──────────────────────────────────────────────────────
# Worker scriptblock for testing a single regular file
# ──────────────────────────────────────────────────────
$testOneFileBlock = {
	param($filePath, $fileName, $baseName, $flashCppPath, $linkerPath, $libPath1, $libPath2, $libPath3, $hasMain, $expectedLinkFailures, $expectedCompileFailures, $expectedRuntimeCrashes, $resultDir)

	$ErrorActionPreference = "SilentlyContinue"
	$objFile = "$baseName.obj"
	$exeFile = "$baseName.exe"
	$ilkFile = "$baseName.ilk"
	$pdbFile = "$baseName.pdb"

	# Clean up previous artifacts
	if (Test-Path $objFile) { Remove-Item $objFile -Force }
	if (Test-Path $exeFile) { Remove-Item $exeFile -Force }
	if (Test-Path $ilkFile) { Remove-Item $ilkFile -Force }
	if (Test-Path $pdbFile) { Remove-Item $pdbFile -Force }

	# Parse expected return value from filename
	$expectedReturnValue = $null
	if ($fileName -match '_ret(\d+)\.cpp$') {
		$expectedReturnValue = [int]$matches[1]
	}

	# Compile with FlashCpp
	$flashCppArgs = @("--log-level=1", $filePath)
	if ($fileName -match "^test_no_access_control_flag_ret100.*\.cpp$") {
		$flashCppArgs = @("-fno-access-control") + $flashCppArgs
	}

	$compileOutput = & $flashCppPath $flashCppArgs 2>&1 | Out-String
	$compileExitCode = $LASTEXITCODE

	$resultLine = ""

	if (Test-Path $objFile) {
		if (-not $hasMain) {
			$resultLine = "RETURN_OK|$fileName|0|no main"
		} else {
			# Link
			$linkArgs = @(
				"/LIBPATH:$libPath1",
				"/SUBSYSTEM:CONSOLE",
				"/OUT:$exeFile",
				$objFile,
				"kernel32.lib",
				"libucrt.lib",
				"legacy_stdio_definitions.lib"
			)
			if ($libPath2) { $linkArgs = @("/LIBPATH:$libPath2") + $linkArgs }
			if ($libPath3) { $linkArgs = @("/LIBPATH:$libPath3") + $linkArgs }

			$linkOutput = & $linkerPath $linkArgs 2>&1 | Out-String

			if ($LASTEXITCODE -eq 0 -and (Test-Path $exeFile)) {
				$exeFullPath = (Resolve-Path $exeFile).Path
				$stdErrOutput = & $exeFullPath 2>&1 | Out-String
				$returnValue = $LASTEXITCODE

				$windowsExceptionCodes = @(
					-1073741819, -1073740791, -1073741571, -1073740940, -1073741795
				)
				$isWindowsCrash = $windowsExceptionCodes -contains $returnValue

				if ($isWindowsCrash) {
					if ($expectedRuntimeCrashes -contains $fileName) {
						$resultLine = "EXPECTED_CRASH|$fileName|"
					} else {
						$signal = if ($returnValue -lt 0) { $returnValue + 2147483648 } else { $returnValue }
						$resultLine = "RUNTIME_CRASH|$fileName|0x$($signal.ToString('X8'))"
					}
				} else {
					$returnValue = $returnValue -band 0xFF
					if ($expectedReturnValue -ne $null) {
						if ($returnValue -ne $expectedReturnValue) {
							$resultLine = "RETURN_MISMATCH|$fileName|$expectedReturnValue|$returnValue"
						} else {
							$resultLine = "RETURN_OK|$fileName|$returnValue|"
						}
					} else {
						$resultLine = "RETURN_OK|$fileName|$returnValue|"
					}
				}

				if (Test-Path $exeFile) { Remove-Item $exeFile -Force -ErrorAction SilentlyContinue }
				if (Test-Path $ilkFile) { Remove-Item $ilkFile -Force -ErrorAction SilentlyContinue }
				if (Test-Path $pdbFile) { Remove-Item $pdbFile -Force -ErrorAction SilentlyContinue }
			} else {
				if ($expectedLinkFailures -contains $fileName) {
					$resultLine = "EXPECTED_LINK_FAIL|$fileName|"
				} else {
					$errors = ($linkOutput -split "`n" | Where-Object { $_ -match "error" } | Select-Object -Last 5) -join "`n"
					$resultLine = "LINK_FAIL|$fileName|$errors"
				}
			}
		}
	} else {
		if ($expectedCompileFailures -contains $fileName) {
			$resultLine = "EXPECTED_COMPILE_FAIL|$fileName|"
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
	}

	if (Test-Path $objFile) { Remove-Item $objFile -Force -ErrorAction SilentlyContinue }

	# Write result
	$resultFile = Join-Path $resultDir "$fileName.result"
	Set-Content -Path $resultFile -Value $resultLine -NoNewline
}

# ──────────────────────────────────────────────────────
# Worker scriptblock for testing a single _fail file
# ──────────────────────────────────────────────────────
$testOneFailFileBlock = {
	param($filePath, $fileName, $baseName, $flashCppPath, $resultDir)

	$ErrorActionPreference = "SilentlyContinue"
	$objFile = "$baseName.obj"
	$ilkFile = "$baseName.ilk"
	$pdbFile = "$baseName.pdb"

	if (Test-Path $objFile) { Remove-Item $objFile -Force }
	if (Test-Path $ilkFile) { Remove-Item $ilkFile -Force }
	if (Test-Path $pdbFile) { Remove-Item $pdbFile -Force }

	$failOutput = & $flashCppPath --log-level=1 $filePath 2>&1 | Out-String

	$resultLine = ""
	if (Test-Path $objFile) {
		$resultLine = "FAIL_BAD|$fileName|should have failed"
	} else {
		$resultLine = "FAIL_OK|$fileName|"
	}

	# Clean up any artifacts
	if (Test-Path $objFile) { Remove-Item $objFile -Force -ErrorAction SilentlyContinue }
	if (Test-Path $ilkFile) { Remove-Item $ilkFile -Force -ErrorAction SilentlyContinue }
	if (Test-Path $pdbFile) { Remove-Item $pdbFile -Force -ErrorAction SilentlyContinue }

	$resultFile = Join-Path $resultDir "$fileName.result"
	Set-Content -Path $resultFile -Value $resultLine -NoNewline
}

# ──────────────────────────────────────────────────────
# Run regular tests
# ──────────────────────────────────────────────────────
if ($useParallel) {
	Write-Host "Running $totalFiles tests with $Jobs parallel jobs (PowerShell $($PSVersionTable.PSVersion.Major))..."
	$referenceFiles | ForEach-Object -ThrottleLimit $Jobs -Parallel {
		$file = $_
		$block = $using:testOneFileBlock
		$hasMain = ($using:mainFileCache)[$file.Name]
		& $block $file.FullName $file.Name $file.BaseName $using:flashCppPath $using:linkerPath $using:libPath1 $using:libPath2 $using:libPath3 $hasMain $using:expectedLinkFailures $using:expectedCompileFailures $using:expectedRuntimeCrashes $using:resultDir
	}
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
		& $testOneFileBlock $file.FullName $file.Name $file.BaseName $flashCppPath $linkerPath $libPath1 $libPath2 $libPath3 $hasMain $expectedLinkFailures $expectedCompileFailures $expectedRuntimeCrashes $resultDir

		# Read and display result inline for sequential mode
		$resultFile = Join-Path $resultDir "$($file.Name).result"
		if (Test-Path $resultFile) {
			$line = Get-Content $resultFile -Raw
			$parts = $line -split '\|', 4
			switch ($parts[0]) {
				"RETURN_OK"             { Write-Host "OK (returned $($parts[2]))" }
				"RETURN_MISMATCH"       { Write-Host "[RETURN MISMATCH] expected $($parts[2]) got $($parts[3])" -ForegroundColor Red }
				"RUNTIME_CRASH"         { Write-Host "[RUNTIME CRASH] $($parts[2])" -ForegroundColor Red }
				"EXPECTED_CRASH"        { Write-Host "OK (expected runtime crash)" }
				"LINK_FAIL"             { Write-Host "[LINK FAILED]" -ForegroundColor Red }
				"EXPECTED_LINK_FAIL"    { Write-Host "OK (expected link fail)" }
				"COMPILE_FAIL"          { Write-Host "[COMPILE FAILED]" -ForegroundColor Red }
				"EXPECTED_COMPILE_FAIL" { Write-Host "OK (expected fail)" }
			}
		}
	}
}

# ──────────────────────────────────────────────────────
# Run _fail tests
# ──────────────────────────────────────────────────────
Write-Host ""
Write-Host "=============================================="
Write-Host "Testing _fail.cpp files (expected to fail)"
Write-Host "=============================================="
Write-Host ""

if ($useParallel -and $failFiles.Count -gt 0) {
	$failFiles | ForEach-Object -ThrottleLimit $Jobs -Parallel {
		$file = $_
		$block = $using:testOneFailFileBlock
		& $block $file.FullName $file.Name $file.BaseName $using:flashCppPath $using:resultDir
	}
} else {
	$currentFile = 0
	foreach ($file in $failFiles) {
		$currentFile++
		Write-Host "[$currentFile/$totalFailFiles] Testing $($file.Name)... " -NoNewline
		& $testOneFailFileBlock $file.FullName $file.Name $file.BaseName $flashCppPath $resultDir

		$resultFile = Join-Path $resultDir "$($file.Name).result"
		if (Test-Path $resultFile) {
			$line = Get-Content $resultFile -Raw
			$parts = $line -split '\|', 3
			switch ($parts[0]) {
				"FAIL_OK"  { Write-Host "OK (failed as expected)" }
				"FAIL_BAD" { Write-Host "[UNEXPECTED SUCCESS - SHOULD FAIL]" -ForegroundColor Red }
			}
		}
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
$linkErrorDetails = @{}

foreach ($file in $referenceFiles) {
	$resultFile = Join-Path $resultDir "$($file.Name).result"
	if (-not (Test-Path $resultFile)) {
		$compileFailed += "$($file.Name) (no result)"
		continue
	}
	$line = Get-Content $resultFile -Raw
	$parts = $line -split '\|', 4
	$status = $parts[0]

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
		"EXPECTED_CRASH" {
			$compileSuccess += $file.Name
			$linkSuccess += $file.Name
			$runSuccess += $file.Name
		}
		"LINK_FAIL" {
			$compileSuccess += $file.Name
			$linkFailed += $file.Name
			if ($useParallel) {
				Write-Host "$($file.Name) - [LINK FAILED]" -ForegroundColor Red
			}
			$linkErrorDetails[$file.Name] = @{
				Errors = @($parts[2])
				Unresolved = @()
				FullOutput = $parts[2]
			}
		}
		"EXPECTED_LINK_FAIL" {
			$compileSuccess += $file.Name
			$linkSuccess += $file.Name
		}
		"COMPILE_FAIL" {
			$compileFailed += $file.Name
			if ($useParallel) {
				Write-Host "$($file.Name) - [COMPILE FAILED]" -ForegroundColor Red
				if ($parts[2]) { Write-Host "  $($parts[2])" -ForegroundColor Yellow }
			}
		}
		"EXPECTED_COMPILE_FAIL" {
			# Don't count expected compile failures
		}
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
				Write-Host "$($file.Name) - [UNEXPECTED SUCCESS - SHOULD FAIL]" -ForegroundColor Red
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
Write-Host "Total files tested: $totalFiles (with $Jobs parallel jobs)"
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
Write-Host "_fail Tests (expected to fail compilation):"
Write-Host "  Failed as expected: $($failTestSuccess.Count)" -ForegroundColor Green
Write-Host "  Unexpectedly passed: $($failTestFailed.Count)" -ForegroundColor Red
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

if ($failTestFailed.Count -gt 0) {
	Write-Host "=== _fail files that unexpectedly succeeded ===" -ForegroundColor Red
	Write-Host "(These files should fail compilation but compiled successfully)" -ForegroundColor Red
	$failTestFailed | Sort-Object | ForEach-Object {
		Write-Host "  - $_"
	}
	Write-Host ""
}

# Check for expected failures that don't contain "fail" in the filename
# These might be legitimate tests that should be fixed rather than permanently excluded
$expectedFailuresWithoutFail = $expectedCompileFailures + $expectedLinkFailures | Where-Object {
	$_ -notmatch "fail"
} | Sort-Object -Unique

if ($expectedFailuresWithoutFail.Count -gt 0) {
	Write-Host "=== Expected failure files without 'fail' in name ===" -ForegroundColor Yellow
	Write-Host "(These files are marked as expected failures but may be legitimate tests to fix)" -ForegroundColor Yellow
	$expectedFailuresWithoutFail | ForEach-Object {
		Write-Host "  - $_" -ForegroundColor Yellow
	}
	Write-Host ""
}

# Exit with error if any compilation or linking failed, if any _fail test unexpectedly passed,
# or if any test crashed at runtime.
$exitCode = 0
$failureReasons = @()

if ($failTestFailed.Count -gt 0) {
	$failureReasons += "Some _fail tests unexpectedly succeeded"
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

if ($failureReasons.Count -gt 0) {
	$exitCode = 1
	Write-Host "RESULT: FAILED - $($failureReasons -join '; ')" -ForegroundColor Red
}
else {
	Write-Host "RESULT: SUCCESS - All files compiled and linked successfully!" -ForegroundColor Green
	Write-Host "                  All _fail tests failed as expected!" -ForegroundColor Green
	if ($runtimeCrashes.Count -eq 0 -and $returnMismatches.Count -eq 0) {
		Write-Host "                  All tests ran successfully!" -ForegroundColor Green
	}
}

Write-Host ""
Write-Host "=============================================="
exit $exitCode
