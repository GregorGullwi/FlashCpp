# Test script specifically for test_lambda_cpp20_comprehensive.cpp
# Compiles, links, and analyzes symbols to validate lambda implementation

Write-Host "=============================================="
Write-Host "Lambda Comprehensive Test - Detailed Analysis"
Write-Host "=============================================="
Write-Host ""

# Navigate to repo root
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RepoRoot = Split-Path -Parent $ScriptDir
Set-Location $RepoRoot

$testFile = "tests\test_lambda_cpp20_comprehensive.cpp"
$objFile = "test_lambda_cpp20_comprehensive.obj"
$exeFile = "test_lambda_cpp20_comprehensive.exe"

# Find FlashCpp compiler
$flashCppPath = ""
if (Test-Path "x64\Debug\FlashCppMSVC.exe") {
    $flashCppPath = "x64\Debug\FlashCppMSVC.exe"
} elseif (Test-Path "x64\Debug\FlashCpp.exe") {
    $flashCppPath = "x64\Debug\FlashCpp.exe"
} else {
    Write-Host "ERROR: FlashCpp not found. Please build it first." -ForegroundColor Red
    exit 1
}

Write-Host "Using compiler: $flashCppPath"
Write-Host "Test file: $testFile"
Write-Host ""

# Clean up previous artifacts
if (Test-Path $objFile) { Remove-Item $objFile -Force }
if (Test-Path $exeFile) { Remove-Item $exeFile -Force }

# Compile
Write-Host "=== COMPILING ===" -ForegroundColor Cyan
$compileOutput = & .\$flashCppPath $testFile 2>&1 | Out-String
Write-Host $compileOutput

if (-not (Test-Path $objFile)) {
    Write-Host ""
    Write-Host "COMPILATION FAILED" -ForegroundColor Red
    exit 1
}

Write-Host "COMPILATION SUCCESSFUL" -ForegroundColor Green
Write-Host ""

# Analyze object file symbols
Write-Host "=== ANALYZING OBJECT FILE ===" -ForegroundColor Cyan

# Find dumpbin
$dumpbinPath = "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.44.35207\bin\Hostx64\x64\dumpbin.exe"
if (-not (Test-Path $dumpbinPath)) {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vswhere) {
        $vsPath = & $vswhere -latest -property installationPath
        if ($vsPath) {
            $possibleDumpbin = Get-ChildItem -Path "$vsPath\VC\Tools\MSVC" -Recurse -Filter "dumpbin.exe" | 
                Where-Object { $_.FullName -match "Hostx64\\x64" } | 
                Select-Object -First 1
            if ($possibleDumpbin) {
                $dumpbinPath = $possibleDumpbin.FullName
            }
        }
    }
}

if (Test-Path $dumpbinPath) {
    # Get symbols
    $symbolOutput = & $dumpbinPath /SYMBOLS $objFile 2>&1 | Out-String
    
    # Count lambda-related symbols
    $lambdaSymbols = $symbolOutput -split "`n" | Where-Object { $_ -match "__lambda" }
    $operatorCallSymbols = $lambdaSymbols | Where-Object { $_ -match "operator\(\)" }
    $invokeSymbols = $lambdaSymbols | Where-Object { $_ -match "__invoke" }
    
    Write-Host "Total lambda-related symbols: $($lambdaSymbols.Count)"
    Write-Host "  operator() symbols: $($operatorCallSymbols.Count)"
    Write-Host "  __invoke symbols: $($invokeSymbols.Count)"
    Write-Host ""
    
    if ($operatorCallSymbols.Count -gt 0) {
        Write-Host "Lambda operator() symbols:" -ForegroundColor Green
        foreach ($sym in $operatorCallSymbols | Select-Object -First 10) {
            Write-Host "  $($sym.Trim())" -ForegroundColor Gray
        }
        if ($operatorCallSymbols.Count -gt 10) {
            Write-Host "  ... and $($operatorCallSymbols.Count - 10) more" -ForegroundColor Gray
        }
        Write-Host ""
    }
    
    # Check for undefined symbols
    $undefinedSymbols = $symbolOutput -split "`n" | Where-Object { $_ -match "UNDEF" -and $_ -match "External" }
    if ($undefinedSymbols.Count -gt 0) {
        Write-Host "Undefined symbols (need linking):" -ForegroundColor Yellow
        foreach ($sym in $undefinedSymbols | Select-Object -First 20) {
            Write-Host "  $($sym.Trim())" -ForegroundColor Gray
        }
        if ($undefinedSymbols.Count -gt 20) {
            Write-Host "  ... and $($undefinedSymbols.Count - 20) more" -ForegroundColor Gray
        }
        Write-Host ""
    }
} else {
    Write-Host "WARNING: dumpbin not found, skipping symbol analysis" -ForegroundColor Yellow
    Write-Host ""
}

# Find linker
$linkerPath = "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.44.35207\bin\Hostx64\x64\link.exe"
if (-not (Test-Path $linkerPath)) {
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
    Write-Host "ERROR: Could not find MSVC linker" -ForegroundColor Red
    exit 1
}

# Get library paths
$vcToolsPath = Split-Path (Split-Path (Split-Path (Split-Path $linkerPath)))
$libPath1 = "$vcToolsPath\lib\x64"

$sdkPath = "C:\Program Files (x86)\Windows Kits\10"
$libPath2 = ""
$libPath3 = ""

if (Test-Path $sdkPath) {
    $sdkVersion = Get-ChildItem -Path "$sdkPath\Lib" -Directory | 
        Sort-Object Name -Descending | 
        Select-Object -First 1
    if ($sdkVersion) {
        $libPath2 = "$sdkPath\Lib\$($sdkVersion.Name)\um\x64"
        $libPath3 = "$sdkPath\Lib\$($sdkVersion.Name)\ucrt\x64"
    }
}

# Link
Write-Host "=== LINKING ===" -ForegroundColor Cyan
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
    Write-Host "LINKING SUCCESSFUL" -ForegroundColor Green
    Write-Host ""
    
    # Run the executable
    Write-Host "=== RUNNING TEST ===" -ForegroundColor Cyan
    $runOutput = & .\$exeFile 2>&1
    $exitCode = $LASTEXITCODE
    
    Write-Host "Exit code: $exitCode"
    if ($exitCode -eq 135) {
        Write-Host "TEST PASSED: All 27 lambda tests returned expected value (27 * 5 = 135)" -ForegroundColor Green
    } else {
        Write-Host "TEST RESULT: Exit code $exitCode" -ForegroundColor Yellow
        Write-Host "Expected: 135 (27 tests * 5 per test)" -ForegroundColor Yellow
    }
    
    if ($runOutput) {
        Write-Host "Output:"
        Write-Host $runOutput
    }
} else {
    Write-Host "LINKING FAILED" -ForegroundColor Red
    Write-Host ""
    Write-Host "=== LINK OUTPUT ===" -ForegroundColor Yellow
    Write-Host $linkOutput
    Write-Host ""
    
    # Extract specific errors
    $errors = $linkOutput -split "`n" | Where-Object { $_ -match "error" }
    if ($errors) {
        Write-Host "=== LINK ERRORS ===" -ForegroundColor Red
        foreach ($err in $errors) {
            Write-Host $err
        }
        Write-Host ""
    }
    
    # Extract unresolved symbols
    $unresolved = $linkOutput -split "`n" | Where-Object { $_ -match "unresolved external symbol" }
    if ($unresolved) {
        Write-Host "=== UNRESOLVED SYMBOLS ===" -ForegroundColor Red
        foreach ($sym in $unresolved) {
            Write-Host $sym
        }
        Write-Host ""
    }
    
    exit 1
}
