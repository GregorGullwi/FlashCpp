# Reference Files Test Script for FlashCpp (PowerShell)
# This script compiles and links all .cpp files in tests/Reference/ and reports any failures

param(
    [string]$TestFile = $null
)

# Suppress PowerShell errors from native commands writing to stderr
# (FlashCpp writes version info to stderr which is not an error)
$ErrorActionPreference = "SilentlyContinue"

# Navigate to the repository root (relative to this script's location)
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RepoRoot = Split-Path -Parent $ScriptDir
Set-Location $RepoRoot

Write-Host "=============================================="
Write-Host "FlashCpp Reference Files Compile & Link Test"
Write-Host "=============================================="
Write-Host ""
Write-Host "Date: $(Get-Date)"
Write-Host ""

# Find the FlashCpp compiler executable
# On GitHub Actions, MSBuild builds FlashCppMSVC.exe
# Locally, build_flashcpp.bat builds FlashCpp.exe
$flashCppPath = ""
if (Test-Path "x64\Debug\FlashCppMSVC.exe") {
    $flashCppPath = "x64\Debug\FlashCppMSVC.exe"
} elseif (Test-Path "x64\Debug\FlashCpp.exe") {
    $flashCppPath = "x64\Debug\FlashCpp.exe"
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
    "test_dwarf_cfi.cpp"  # Uses Linux-specific DWARF/ELF headers
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
Write-Host "Found $totalFiles test files with main() in tests/"
Write-Host "Found $totalFailFiles _fail test files (expected to fail compilation)"
Write-Host ""

# Expected compile failures - files that are intentionally designed to fail compilation
# or use features not yet implemented in FlashCpp
# NOTE: Files with _fail.cpp suffix are automatically tested separately
#
# Intentionally invalid code (tests error detection):
#   - concept_error_test_fail.cpp - Tests constraint error messages
#
# Unimplemented features:
$expectedCompileFailures = @(
  "test_cstddef.cpp"
  "test_cstdio_puts.cpp"
  "test_cstdlib.cpp"
  # test_lambda_cpp20_comprehensive.cpp - Now compiles with unsupported features commented out
)

# Expected link failures - files that compile but have known link issues
# These are typically due to features not yet implemented in FlashCpp
$expectedLinkFailures = @(
    # ABI tests that require external C helper files to be compiled and linked
    "test_external_abi.cpp"
    "test_external_abi_simple.cpp"
    # Self-contained ABI tests (link on Linux but fail on Windows)
    "test_mixed_abi.cpp"
    "test_linux_abi.cpp"  # Tests 6 integer params (Linux ABI specific)
    "test_full_spec_inherit.cpp"          # Demonstrates full specialization inheritance parsing (Priority 8b: implicit constructor issue)
    "test_full_spec_inherit_simple.cpp"   # Demonstrates full specialization inheritance parsing (Priority 8b: implicit constructor issue)
)

# Results tracking
$compileSuccess = @()
$compileFailed = @()
$linkSuccess = @()
$linkFailed = @()
$runSuccess = @()
$runFailed = @()
$missingExpected = @()
# Detailed error tracking
$linkErrorDetails = @{}

$currentFile = 0
foreach ($file in $referenceFiles) {
    $currentFile++
    $baseName = $file.BaseName
    # FlashCpp writes .obj to current directory (repo root), not source directory
    $objFile = "$baseName.obj"
    $exeFile = "$baseName.exe"
    $ilkFile = "$baseName.ilk"

    # Clean up previous artifacts
    if (Test-Path $objFile) { Remove-Item $objFile -Force }
    if (Test-Path $exeFile) { Remove-Item $exeFile -Force }
    if (Test-Path $ilkFile) { Remove-Item $ilkFile -Force }

    Write-Host "[$currentFile/$totalFiles] Testing $($file.Name)... " -NoNewline

    # Compile with FlashCpp
    $compileOutput = & .\$flashCppPath --log-level=Codegen:error $file.FullName 2>&1 | Out-String

    # Check if compilation succeeded by verifying obj file was created
    if (Test-Path $objFile) {
        $compileSuccess += $file.Name

        # Check if source file has a main function before attempting to link
        $sourceContent = Get-Content $file.FullName -Raw
        $hasMain = $sourceContent -match '\bint\s+main\s*\(' -or $sourceContent -match '\bvoid\s+main\s*\('

        if (-not $hasMain) {
            Write-Host "OK (no main - link skipped)"
            $linkSuccess += $file.Name  # Count as success since compilation worked
        }
        else {
            # Try to link
            # Link with common Microsoft runtime libraries
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
                $linkSuccess += $file.Name
                
                # Check for expected return value and run the executable
                $expectedReturnMatch = $sourceContent | Select-String -Pattern '^\s*//\s*EXPECTED_RETURN:\s*(\d+)' | Select-Object -First 1
                
                if ($expectedReturnMatch) {
                    $expectedReturn = [int]$expectedReturnMatch.Matches.Groups[1].Value
                    
                    # Run the executable and check return value
                    & ".\$exeFile" > $null 2>&1
                    $actualReturn = $LASTEXITCODE
                    
                    if ($actualReturn -eq $expectedReturn) {
                        Write-Host "OK (returned $actualReturn)"
                        $runSuccess += $file.Name
                    }
                    else {
                        Write-Host ""
                        Write-Host "[$currentFile/$totalFiles] $($file.Name) - [RUN FAILED] Expected return: $expectedReturn, got: $actualReturn" -ForegroundColor Red
                        $runFailed += $file.Name
                    }
                }
                else {
                    # No expected return value specified
                    Write-Host "OK"
                    $runSuccess += $file.Name
                }
                
                # Clean up after running
                Remove-Item $exeFile -Force -ErrorAction SilentlyContinue
                Remove-Item $ilkFile -Force -ErrorAction SilentlyContinue
            }
            else {
                # Check if this is an expected failure
                if ($expectedLinkFailures -contains $file.Name) {
                    Write-Host "OK (expected link fail)"
                    # Don't count expected failures as actual failures
                    $linkSuccess += $file.Name
                }
                else {
                    Write-Host ""
                    Write-Host "[$currentFile/$totalFiles] $($file.Name) - [LINK FAILED]" -ForegroundColor Red
                    $linkFailed += $file.Name

                    # Extract all errors from link output
                    $errors = ($linkOutput -split "`n" | Where-Object { $_ -match "error" })
                    $unresolved = ($linkOutput -split "`n" | Where-Object { $_ -match "unresolved external symbol" })

                    # Store detailed error info for later display
                    $linkErrorDetails[$file.Name] = @{
                        Errors = $errors
                        Unresolved = $unresolved
                        FullOutput = $linkOutput
                    }

                    # Show last 10 lines of output immediately
                    if ($errors) {
                        Write-Host "    Link errors (last 10):" -ForegroundColor Yellow
                        foreach ($err in ($errors | Select-Object -Last 10)) {
                            Write-Host "      $err" -ForegroundColor Yellow
                        }
                    }
                    # Also show unresolved external symbols
                    if ($unresolved) {
                        Write-Host "    Unresolved symbols (last 10):" -ForegroundColor Yellow
                        foreach ($sym in ($unresolved | Select-Object -Last 10)) {
                            Write-Host "      $sym" -ForegroundColor Yellow
                        }
                    }
                }
            }
        }
    }
    else {
        # Check if this is an expected compile failure
        if ($expectedCompileFailures -contains $file.Name) {
            Write-Host "OK (expected fail)"
            # Don't count expected failures as actual failures
        }
        else {
            Write-Host ""
            Write-Host "[$currentFile/$totalFiles] $($file.Name) - [COMPILE FAILED]" -ForegroundColor Red
            $compileFailed += $file.Name
            # Show compile output to help diagnose the issue
            # Filter out the version banner and non-error lines, prioritize showing errors
            $allLines = $compileOutput -split "`n" | Where-Object {
                $_.Trim() -ne "" -and
                $_ -notmatch "===== FLASHCPP VERSION" -and
                $_ -notmatch "(Compilation Timing|Phase.*Time|Percentage|---|TOTAL|\|)"
            }
            # Try to show error lines first, otherwise show last 10 lines
            $errorLines = $allLines | Where-Object { $_ -match "\[ERROR\]|\[FATAL\]|error:" }
            if ($errorLines) {
                Write-Host "    Error output:" -ForegroundColor Yellow
                foreach ($line in ($errorLines | Select-Object -Last 10)) {
                    Write-Host "    $($line.Trim())" -ForegroundColor Yellow
                }
            } elseif ($allLines.Count -gt 0) {
                Write-Host "    Last 10 lines of output:" -ForegroundColor Yellow
                foreach ($line in ($allLines | Select-Object -Last 10)) {
                    Write-Host "    $($line.Trim())" -ForegroundColor Yellow
                }
            } else {
                Write-Host "    (No error details available - obj file not created)" -ForegroundColor Yellow
            }
        }
    }

    # Clean up obj file after each test (like the shell script does)
    if (Test-Path $objFile) { Remove-Item $objFile -Force -ErrorAction SilentlyContinue }
}

# Test _fail files - these should fail compilation
Write-Host ""
Write-Host "=============================================="
Write-Host "Testing _fail.cpp files (expected to fail)"
Write-Host "=============================================="
Write-Host ""

# Results tracking for fail tests
$failTestSuccess = @()
$failTestFailed = @()

$currentFile = 0
foreach ($file in $failFiles) {
    $currentFile++
    $baseName = $file.BaseName
    $objFile = "$baseName.obj"
    $ilkFile = "$baseName.ilk"

    # Clean up previous artifacts
    if (Test-Path $objFile) { Remove-Item $objFile -Force }
    if (Test-Path $ilkFile) { Remove-Item $ilkFile -Force }

    Write-Host "[$currentFile/$totalFailFiles] Testing $($file.Name)... " -NoNewline

    # Compile with FlashCpp - we EXPECT this to fail
    $compileOutput = & .\$flashCppPath --log-level=Codegen:error $file.FullName 2>&1 | Out-String

    # Check if compiler crashed (exit codes: 134=SIGABRT, 136=SIGFPE, 139=SIGSEGV)
    if ($LASTEXITCODE -eq 134 -or $LASTEXITCODE -eq 136 -or $LASTEXITCODE -eq 139) {
        Write-Host ""
        Write-Host "[$currentFile/$totalFailFiles] $($file.Name) - [COMPILER CRASH - SHOULD FAIL CLEANLY]" -ForegroundColor Red
        $failTestFailed += "$($file.Name) (CRASHED)"
        # Clean up any artifacts
        if (Test-Path $objFile) { Remove-Item $objFile -Force -ErrorAction SilentlyContinue }
        if (Test-Path $ilkFile) { Remove-Item $ilkFile -Force -ErrorAction SilentlyContinue }
        continue
    }

    # Check if compilation succeeded (which is BAD for _fail tests)
    if (Test-Path $objFile) {
        Write-Host ""
        Write-Host "[$currentFile/$totalFailFiles] $($file.Name) - [UNEXPECTED SUCCESS - SHOULD FAIL]" -ForegroundColor Red
        $failTestFailed += $file.Name
        # Clean up the object file
        Remove-Item $objFile -Force -ErrorAction SilentlyContinue
        Remove-Item $ilkFile -Force -ErrorAction SilentlyContinue
    }
    else {
        Write-Host "OK (failed as expected)"
        $failTestSuccess += $file.Name
    }

    # Clean up any artifacts
    if (Test-Path $objFile) { Remove-Item $objFile -Force -ErrorAction SilentlyContinue }
    if (Test-Path $ilkFile) { Remove-Item $ilkFile -Force -ErrorAction SilentlyContinue }
}

# Summary
Write-Host ""
Write-Host "=============================================="
Write-Host "                   SUMMARY"
Write-Host "=============================================="
Write-Host ""
Write-Host "Total files tested: $totalFiles"
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
Write-Host "  Running (of successfully linked files):"
Write-Host "    Success: $($runSuccess.Count)" -ForegroundColor Green
Write-Host "    Failed:  $($runFailed.Count)" -ForegroundColor Red
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

if ($runFailed.Count -gt 0) {
    Write-Host "=== Files that returned wrong value ===" -ForegroundColor Red
    $runFailed | Sort-Object | ForEach-Object {
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

# Exit with error if any compilation or linking failed, or if any _fail test unexpectedly passed
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
if ($runFailed.Count -gt 0) {
    $failureReasons += "Some files returned wrong values"
}

if ($failureReasons.Count -gt 0) {
    $exitCode = 1
    Write-Host "RESULT: FAILED - $($failureReasons -join '; ')" -ForegroundColor Red
}
else {
    Write-Host "RESULT: SUCCESS - All files compiled, linked, and ran successfully!" -ForegroundColor Green
    Write-Host "                  All _fail tests failed as expected!" -ForegroundColor Green
}

Write-Host ""
Write-Host "=============================================="
exit $exitCode
