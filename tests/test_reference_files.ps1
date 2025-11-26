# Reference Files Test Script for FlashCpp (PowerShell)
# This script compiles and links all .cpp files in tests/Reference/ and reports any failures

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

# Get all .cpp files from tests/Reference
$referenceFiles = Get-ChildItem -Path "tests\Reference" -Filter "*.cpp" | Sort-Object Name

$totalFiles = $referenceFiles.Count
Write-Host "Found $totalFiles test files in tests/Reference/"
Write-Host ""

# Results tracking
$compileSuccess = @()
$compileFailed = @()
$linkSuccess = @()
$linkFailed = @()

$currentFile = 0
foreach ($file in $referenceFiles) {
    $currentFile++
    $baseName = $file.BaseName
    # FlashCpp writes .obj to current directory (repo root), not source directory
    $objFile = "$baseName.obj"
    $exeFile = "$baseName.exe"
    
    Write-Host "[$currentFile/$totalFiles] Testing: $($file.Name)"
    
    # Clean up previous artifacts
    if (Test-Path $objFile) { Remove-Item $objFile -Force }
    if (Test-Path $exeFile) { Remove-Item $exeFile -Force }
    
    # Compile with FlashCpp
    $compileOutput = & .\$flashCppPath $file.FullName 2>&1 | Out-String
    
    # Check if compilation succeeded by verifying obj file was created
    if (Test-Path $objFile) {
        Write-Host "  [COMPILE OK]" -ForegroundColor Green
        $compileSuccess += $file.Name
        
        # Check if source file has a main function before attempting to link
        $sourceContent = Get-Content $file.FullName -Raw
        $hasMain = $sourceContent -match '\bint\s+main\s*\(' -or $sourceContent -match '\bvoid\s+main\s*\('
        
        if (-not $hasMain) {
            Write-Host "  [LINK SKIPPED - no main()]" -ForegroundColor Cyan
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
                "libcmt.lib",
                "libvcruntime.lib",
                "libucrt.lib",
                "legacy_stdio_definitions.lib"
            )
            
            if ($libPath2) { $linkArgs = @("/LIBPATH:$libPath2") + $linkArgs }
            if ($libPath3) { $linkArgs = @("/LIBPATH:$libPath3") + $linkArgs }
            
            $linkOutput = & $linkerPath $linkArgs 2>&1 | Out-String
            
            if ($LASTEXITCODE -eq 0 -and (Test-Path $exeFile)) {
                Write-Host "  [LINK OK]" -ForegroundColor Green
                $linkSuccess += $file.Name
            }
            else {
                Write-Host "  [LINK FAILED]" -ForegroundColor Red
                $linkFailed += $file.Name
                # Extract first error from link output
                $firstError = ($linkOutput -split "`n" | Where-Object { $_ -match "error" } | Select-Object -First 1)
                if ($firstError) {
                    Write-Host "    Error: $firstError" -ForegroundColor Yellow
                }
            }
        }
    }
    else {
        Write-Host "  [COMPILE FAILED]" -ForegroundColor Red
        $compileFailed += $file.Name
        # Extract first error from compile output
        $firstError = ($compileOutput -split "`n" | Where-Object { $_ -match "(error|Error)" } | Select-Object -First 1)
        if ($firstError) {
            Write-Host "    Error: $firstError" -ForegroundColor Yellow
        }
    }
    
    Write-Host ""
}

# Summary
Write-Host ""
Write-Host "=============================================="
Write-Host "                   SUMMARY"
Write-Host "=============================================="
Write-Host ""
Write-Host "Total files tested: $totalFiles"
Write-Host ""
Write-Host "Compilation:"
Write-Host "  Success: $($compileSuccess.Count)" -ForegroundColor Green
Write-Host "  Failed:  $($compileFailed.Count)" -ForegroundColor Red
Write-Host ""
Write-Host "Linking (of successfully compiled files):"
Write-Host "  Success: $($linkSuccess.Count)" -ForegroundColor Green
Write-Host "  Failed:  $($linkFailed.Count)" -ForegroundColor Red
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
}

# Exit with error if any compilation or linking failed
$exitCode = 0
if ($compileFailed.Count -gt 0 -or $linkFailed.Count -gt 0) {
    $exitCode = 1
    Write-Host "RESULT: FAILED - Some files did not compile or link successfully" -ForegroundColor Red
}
else {
    Write-Host "RESULT: SUCCESS - All files compiled and linked successfully!" -ForegroundColor Green
}

Write-Host ""
Write-Host "=============================================="
exit $exitCode
