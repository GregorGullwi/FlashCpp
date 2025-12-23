# Standard Header Test Script for FlashCpp (PowerShell)
# This script tests inclusion of various standard headers and generates a report

# Navigate to the repository root (relative to this script's location)
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RepoRoot = Split-Path -Parent $ScriptDir
Set-Location $RepoRoot

# Build the compiler if not built (Windows)
if (-not (Test-Path "x64\Debug\FlashCpp.exe")) {
    Write-Host "Building FlashCpp..."
    & .\build_flashcpp.bat
}

# Detect Visual Studio installation using vswhere
$vswherePath = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$IncludePaths = @()

if (Test-Path $vswherePath) {
    try {
        $vsPath = & $vswherePath -latest -property installationPath
        if ($vsPath) {
            Write-Host "Found Visual Studio at: $vsPath"
            
            # Find MSVC version
            $msvcBasePath = Join-Path $vsPath "VC\Tools\MSVC"
            if (Test-Path $msvcBasePath) {
                $msvcVersion = (Get-ChildItem $msvcBasePath | Sort-Object Name -Descending | Select-Object -First 1).Name
                Write-Host "  MSVC version: $msvcVersion"
                
                $msvcInclude = Join-Path $msvcBasePath "$msvcVersion\include"
                if (Test-Path $msvcInclude) {
                    $IncludePaths += "-I$msvcInclude"
                }
            }
            
            # Find Windows SDK version
            $sdkBasePath = "${env:ProgramFiles(x86)}\Windows Kits\10\Include"
            if (Test-Path $sdkBasePath) {
                $sdkVersion = (Get-ChildItem $sdkBasePath | Where-Object { $_.Name -match '^\d+\.\d+\.\d+\.\d+$' } | Sort-Object Name -Descending | Select-Object -First 1).Name
                if ($sdkVersion) {
                    Write-Host "  Windows SDK: $sdkVersion"
                    $ucrtPath = Join-Path $sdkBasePath "$sdkVersion\ucrt"
                    $sharedPath = Join-Path $sdkBasePath "$sdkVersion\shared"
                    $umPath = Join-Path $sdkBasePath "$sdkVersion\um"
                    
                    if (Test-Path $ucrtPath) { $IncludePaths += "-I$ucrtPath" }
                    if (Test-Path $sharedPath) { $IncludePaths += "-I$sharedPath" }
                    if (Test-Path $umPath) { $IncludePaths += "-I$umPath" }
                }
            }
        }
    }
    catch {
        Write-Host "Warning: Error detecting Visual Studio paths: $_"
    }
}

# Fallback if detection failed
if ($IncludePaths.Count -eq 0) {
    Write-Host "Warning: Could not auto-detect include paths, using fallback"
    $IncludePaths = @(
        "-IC:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.44.35207\include",
        "-IC:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\ucrt",
        "-IC:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\shared",
        "-IC:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\um"
    )
}

# Test headers
$Headers = @(
    # C library wrappers
    "cstddef",
    "cstdint",
    "cstdlib",
    "cstring",
    "climits",
    "cstdio",
    "cmath",
    "cassert",
    "cerrno",
    "cfloat",
    
    # C++ utilities
    "utility",
    "type_traits",
    "limits",
    "initializer_list",
    "tuple",
    "any",
    "optional",
    "variant",
    
    # Containers
    "array",
    "vector",
    "deque",
    "list",
    "forward_list",
    "set",
    "map",
    "unordered_set",
    "unordered_map",
    "stack",
    "queue",
    
    # Strings
    "string",
    "string_view",
    
    # I/O
    "iostream",
    "istream",
    "ostream",
    "sstream",
    "fstream",
    
    # Memory
    "memory",
    "new",
    
    # Algorithms and functional
    "algorithm",
    "functional",
    "numeric",
    
    # Iterators
    "iterator",
    
    # Time
    "chrono",
    
    # Concepts (C++20)
    "concepts",
    
    # Ranges (C++20)
    "ranges",
    
    # Format (C++20)
    "format",
    
    # Coroutines (C++20)
    "coroutine",
    
    # Other C++20 headers
    "span",
    "bit",
    "compare",
    "source_location",
    "version",
    
    # Threading
    "thread",
    "mutex",
    "atomic",
    "condition_variable",
    "future",
    
    # Filesystem (C++17)
    "filesystem",
    
    # Regular expressions
    "regex",
    
    # Exception handling
    "exception",
    "stdexcept"
)

Write-Host "=============================================="
Write-Host "FlashCpp Standard Header Support Test Report"
Write-Host "=============================================="
Write-Host ""
Write-Host "Date: $(Get-Date)"
if (Test-Path "x64\Debug\FlashCpp.exe") {
    $buildDate = (Get-Item "x64\Debug\FlashCpp.exe").LastWriteTime
    Write-Host "FlashCpp built: $buildDate"
}
else {
    Write-Host "FlashCpp built: unknown"
}

# Check for compiler version
if (Get-Command "cl.exe" -ErrorAction SilentlyContinue) {
    $clVersion = & cl 2>&1 | Select-Object -First 1
    Write-Host "MSVC version: $clVersion"
}
elseif (Get-Command "clang++" -ErrorAction SilentlyContinue) {
    $clangVersion = & clang++ --version | Select-Object -First 1
    Write-Host "Clang version: $clangVersion"
}
Write-Host ""

# Results
$successHeaders = @{}
$failedHeaders = @{}
$errorMessages = @{}

foreach ($header in $Headers) {
    # Break after 3 errors (temporary)
    if ($failedHeaders.Count -ge 3) {
        Write-Host "`nStopping after 3 errors (temporary limit)..." -ForegroundColor Yellow
        break
    }
    
    # Create test file
    @"
#include <$header>
int main() { return 0; }
"@ | Out-File -FilePath "test_header_temp.cpp" -Encoding ASCII
    
    # Run FlashCpp and capture output
    # Build argument list properly to preserve paths with spaces
    $args = @('test_header_temp.cpp') + $IncludePaths
    
    $exitCode = 0
    # Suppress verbose debug output, only capture errors
    $output = & .\x64\Debug\FlashCpp.exe @args 2>&1 | Where-Object { 
        $_ -notmatch "^DEBUG:" -and 
        $_ -notmatch "^===" -and
        $_ -notmatch "^Adding \d+ bytes" -and
        $_ -notmatch "^Section \d+" -and
        $_ -notmatch "^Symbol \d+" -and
        $_ -notmatch "Machine code bytes"
    }
    $exitCode = $LASTEXITCODE
    $outputStr = $output | Out-String
    
    # Check for success - look for "Object file written successfully"
    if ($exitCode -eq 0 -and $outputStr -match "Object file written successfully") {
        $successHeaders[$header] = $true
        Write-Host "[PASS] <$header>" -ForegroundColor Green
    }
    else {
        $failedHeaders[$header] = $true
        # Extract first meaningful error message
        $errorLines = $output | Where-Object { $_ -and $_ -match "(Error|error|Failed|Internal compiler|Invalid|terminate|Exception)" }
        if ($errorLines) {
            $firstError = ($errorLines | Select-Object -First 1) -replace '^\s+', ''
        }
        else {
            $firstError = "Unknown error (exit code: $exitCode)"
        }
        $errorMessages[$header] = $firstError
        Write-Host "[FAIL] <$header>" -ForegroundColor Red
        if ($firstError.Length -lt 120) {
            Write-Host "       $firstError" -ForegroundColor DarkRed
        }
    }
}

# Clean up temp files
if (Test-Path "test_header_temp.cpp") { Remove-Item "test_header_temp.cpp" }
if (Test-Path "test_header_temp.obj") { Remove-Item "test_header_temp.obj" }

Write-Host ""
Write-Host "=============================================="
Write-Host "                   SUMMARY"
Write-Host "=============================================="
Write-Host ""

# Count results
$passCount = $successHeaders.Count
$failCount = $failedHeaders.Count
$total = $passCount + $failCount

Write-Host "Total headers tested: $total"
Write-Host "Passed: $passCount" -ForegroundColor Green
Write-Host "Failed: $failCount" -ForegroundColor Red
Write-Host ""

if ($passCount -gt 0) {
    Write-Host "=== Successfully Included Headers ===" -ForegroundColor Green
    $successHeaders.Keys | Sort-Object | ForEach-Object {
        Write-Host "  - <$_>"
    }
    Write-Host ""
}

if ($failCount -gt 0) {
    Write-Host "=== Failed Headers ===" -ForegroundColor Red
    $failedHeaders.Keys | Sort-Object | ForEach-Object {
        Write-Host "  - <$_>"
        Write-Host "    Error: $($errorMessages[$_])"
    }
    Write-Host ""
}
