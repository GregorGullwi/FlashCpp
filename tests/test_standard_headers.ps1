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

# Auto-detect include paths by querying the compiler
# This makes the script portable across different systems
function Get-IncludePaths {
    $paths = @()
    
    # Try to detect MSVC include paths
    if (Get-Command "cl.exe" -ErrorAction SilentlyContinue) {
        try {
            # Get MSVC environment
            $output = & cmd /c "echo | cl /Bv /E 2>&1"
            $inIncludeSection = $false
            
            foreach ($line in $output -split "`n") {
                if ($line -match "INCLUDE=") {
                    $includeDirs = $line -replace "INCLUDE=", "" -split ";"
                    foreach ($dir in $includeDirs) {
                        $dir = $dir.Trim()
                        if ($dir -and (Test-Path $dir)) {
                            $paths += "-I`"$dir`""
                        }
                    }
                    break
                }
            }
        }
        catch {
            Write-Host "Warning: Could not auto-detect MSVC include paths"
        }
    }
    
    # Try to detect Clang include paths (if available on Windows)
    if ($paths.Count -eq 0 -and (Get-Command "clang++" -ErrorAction SilentlyContinue)) {
        try {
            $output = & cmd /c "echo | clang++ -E -x c++ - -v 2>&1"
            $inSearchSection = $false
            
            foreach ($line in $output -split "`n") {
                if ($line -match "#include <...> search starts here:") {
                    $inSearchSection = $true
                    continue
                }
                if ($line -match "End of search list") {
                    break
                }
                if ($inSearchSection -and $line.Trim()) {
                    $path = $line.Trim()
                    if (Test-Path $path) {
                        $paths += "-I`"$path`""
                    }
                }
            }
        }
        catch {
            Write-Host "Warning: Could not auto-detect Clang include paths"
        }
    }
    
    return $paths -join " "
}

# Try to auto-detect, fall back to common paths if detection fails
$IncludePaths = Get-IncludePaths
if (-not $IncludePaths) {
    Write-Host "Warning: Could not auto-detect include paths, using fallback paths"
    # Fallback to common Windows MSVC paths - adjust these for your system
    $IncludePaths = "-I`"C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.44.35207\include`" " +
                    "-I`"C:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\ucrt`" " +
                    "-I`"C:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\shared`" " +
                    "-I`"C:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\um`""
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
    # Create test file
    @"
#include <$header>
int main() { return 0; }
"@ | Out-File -FilePath "test_header_temp.cpp" -Encoding ASCII
    
    # Run FlashCpp and capture output
    $output = & .\x64\Debug\FlashCpp.exe test_header_temp.cpp $IncludePaths.Split() 2>&1 | Out-String
    
    # Check for success - look for "Object file written successfully"
    if ($output -match "Object file written successfully") {
        $successHeaders[$header] = $true
        Write-Host "[PASS] <$header>" -ForegroundColor Green
    }
    else {
        $failedHeaders[$header] = $true
        # Extract first error message
        $firstError = ($output -split "`n" | Where-Object { $_ -match "(Error|error|Internal compiler error|Invalid|terminate)" } | Select-Object -First 1)
        $errorMessages[$header] = $firstError
        Write-Host "[FAIL] <$header>: $firstError" -ForegroundColor Red
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

Write-Host "=============================================="
Write-Host "         ISSUES TO FIX FOR STD HEADERS"
Write-Host "=============================================="
Write-Host ""
Write-Host "Based on the test results, the following issues need to be addressed:"
Write-Host ""

# Analyze common error patterns
Write-Host "1. PREPROCESSOR ISSUES:"
Write-Host "   - Feature test macros (__cpp_*) need to be defined"
Write-Host "   - __has_feature/__has_builtin/__has_cpp_attribute intrinsics not handled"
Write-Host "   - __SANITIZE_THREAD__ and similar sanitizer macros"
Write-Host ""

Write-Host "2. PARSER/LEXER ISSUES:"
Write-Host "   - Complex preprocessor expressions with << operator in conditions"
Write-Host "   - Some C++20 syntax may not be fully supported"
Write-Host ""

Write-Host "3. BUILTIN FUNCTIONS:"
Write-Host "   - __builtin_* functions need to be recognized"
Write-Host "   - Compiler intrinsics used by standard library"
Write-Host ""

Write-Host "=============================================="
Write-Host "              END OF REPORT"
Write-Host "=============================================="
