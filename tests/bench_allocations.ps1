# Print global operator new/delete stats from a compile with --alloc-stats.
# Requires a Sharded build compiled with FLASHCPP_TRACK_ALLOCATIONS=1.
#
# Usage:
#   pwsh tests/bench_allocations.ps1 [test.cpp]
#
# Example:
#   .\build_flashcpp.bat
#   pwsh tests/bench_allocations.ps1 tests/std/test_std_type_traits.cpp

param(
	[Parameter(Position = 0)]
	[string]$TestFile = "tests/std/test_std_type_traits.cpp"
)

$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RepoRoot = Split-Path -Parent $ScriptDir
Set-Location $RepoRoot

$Compiler = Join-Path $RepoRoot "x64\Sharded\FlashCppMSVC.exe"
if (-not (Test-Path $Compiler)) {
	Write-Error "Compiler not found: $Compiler (run .\build_flashcpp.bat first)"
}

$TestPath = Resolve-Path $TestFile

Write-Host "Running allocation benchmark: $TestPath"
$output = & $Compiler --alloc-stats $TestPath 2>&1 | Out-String

if ($LASTEXITCODE -ne 0) {
	Write-Host $output
	throw "Compile failed with exit code $LASTEXITCODE"
}

$allocMatch = [regex]::Match($output, "allocations=(\d+)")
$bytesMatch = [regex]::Match($output, "bytes allocated=(\d+)")
if (-not $allocMatch.Success -or -not $bytesMatch.Success) {
	Write-Host $output
	throw "Could not parse allocation stats (rebuild with FLASHCPP_TRACK_ALLOCATIONS=1 and pass --alloc-stats)"
}

Write-Host "allocations=$($allocMatch.Groups[1].Value)"
Write-Host "bytes_allocated=$($bytesMatch.Groups[1].Value)"
