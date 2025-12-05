# PowerShell script to analyze object file symbols using dumpbin
# Usage: .\analyze_obj_symbols.ps1 <file.obj>

param(
    [Parameter(Mandatory=$true)]
    [string]$ObjFile
)

if (-not (Test-Path $ObjFile)) {
    Write-Host "ERROR: File not found: $ObjFile" -ForegroundColor Red
    exit 1
}

# Find dumpbin.exe
$dumpbinPath = "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.44.35207\bin\Hostx64\x64\dumpbin.exe"
if (-not (Test-Path $dumpbinPath)) {
    # Try to find it dynamically
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

if (-not (Test-Path $dumpbinPath)) {
    Write-Host "ERROR: Could not find dumpbin.exe" -ForegroundColor Red
    exit 1
}

Write-Host "=============================================="
Write-Host "Object File Symbol Analysis"
Write-Host "=============================================="
Write-Host "File: $ObjFile"
Write-Host ""

# Analyze symbols
Write-Host "=== SYMBOLS ===" -ForegroundColor Cyan
$symbolOutput = & $dumpbinPath /SYMBOLS $ObjFile 2>&1 | Out-String
Write-Host $symbolOutput

Write-Host ""
Write-Host "=== SUMMARY ===" -ForegroundColor Cyan

# Extract and categorize symbols
$lines = $symbolOutput -split "`n"
$definedSymbols = @()
$undefinedSymbols = @()
$externSymbols = @()

foreach ($line in $lines) {
    if ($line -match '^\s*[0-9A-F]+\s+\w+\s+(SECT|UNDEF|External)\s+') {
        if ($line -match 'UNDEF') {
            $undefinedSymbols += $line.Trim()
        }
        elseif ($line -match 'External') {
            if ($line -match 'UNDEF') {
                $undefinedSymbols += $line.Trim()
            }
            else {
                $externSymbols += $line.Trim()
            }
        }
        elseif ($line -match 'SECT') {
            $definedSymbols += $line.Trim()
        }
    }
}

Write-Host "Defined symbols (exported): $($definedSymbols.Count)" -ForegroundColor Green
if ($definedSymbols.Count -gt 0 -and $definedSymbols.Count -le 20) {
    foreach ($sym in $definedSymbols) {
        Write-Host "  $sym" -ForegroundColor Gray
    }
}

Write-Host ""
Write-Host "External symbols (imported): $($externSymbols.Count)" -ForegroundColor Yellow
if ($externSymbols.Count -gt 0 -and $externSymbols.Count -le 20) {
    foreach ($sym in $externSymbols) {
        Write-Host "  $sym" -ForegroundColor Gray
    }
}

Write-Host ""
Write-Host "Undefined symbols (need resolution): $($undefinedSymbols.Count)" -ForegroundColor Red
if ($undefinedSymbols.Count -gt 0) {
    foreach ($sym in $undefinedSymbols) {
        Write-Host "  $sym" -ForegroundColor Gray
    }
}

# Show sections
Write-Host ""
Write-Host "=== SECTIONS ===" -ForegroundColor Cyan
$sectionOutput = & $dumpbinPath /HEADERS $ObjFile 2>&1 | Out-String
$sectionLines = $sectionOutput -split "`n" | Where-Object { $_ -match "SECTION HEADER" -or $_ -match "^\s+\.\w+" }
foreach ($line in $sectionLines) {
    Write-Host $line
}

Write-Host ""
Write-Host "=== DISASSEMBLY (first 50 lines) ===" -ForegroundColor Cyan
$disasmOutput = & $dumpbinPath /DISASM $ObjFile 2>&1 | Out-String
$disasmLines = $disasmOutput -split "`n" | Select-Object -First 50
foreach ($line in $disasmLines) {
    Write-Host $line
}
