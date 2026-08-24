param(
	[ValidateSet("clang", "msvc", "all")]
	[string]$Toolchain = "all",
	[string]$OutputDirectory = ""
)

$ErrorActionPreference = "Stop"
$scriptDirectory = Split-Path -Parent $MyInvocation.MyCommand.Path
if (-not $OutputDirectory) {
	$OutputDirectory = Join-Path ([System.IO.Path]::GetTempPath()) "flashcpp_parallel_frontend_benchmarks"
}
$outputPath = [System.IO.Path]::GetFullPath($OutputDirectory)
[System.IO.Directory]::CreateDirectory($outputPath) | Out-Null

function Invoke-Checked([scriptblock]$Command) {
	& $Command
	if ($LASTEXITCODE -ne 0) {
		throw "Benchmark build failed with exit code $LASTEXITCODE"
	}
}

function Build-WithClang {
	$clang = (Get-Command clang++.exe -ErrorAction SilentlyContinue).Source
	if (-not $clang) {
		$knownClang = "C:\Program Files\LLVM\bin\clang++.exe"
		if (Test-Path -LiteralPath $knownClang) { $clang = $knownClang }
	}
	if (-not $clang) { throw "clang++.exe was not found" }

	Invoke-Checked { & $clang -std=c++20 -O2 -Wall -Wextra -Wshadow -Werror `
		(Join-Path $scriptDirectory "query_benchmark.cpp") -o (Join-Path $outputPath "query_clang.exe") }
	Invoke-Checked { & $clang -std=c++20 -O2 -Wall -Wextra -Wshadow -Werror `
		(Join-Path $scriptDirectory "interner_benchmark.cpp") -o (Join-Path $outputPath "interner_clang.exe") }
	Invoke-Checked { & $clang -std=c++20 -O2 -Wall -Wextra -Wshadow -Werror `
		(Join-Path $scriptDirectory "benchmark_smoke.cpp") `
		(Join-Path $scriptDirectory "benchmark_harness.cpp") -o (Join-Path $outputPath "harness_clang.exe") }
}

function Import-MsvcEnvironment {
	$vcvars = "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
	if (-not (Test-Path -LiteralPath $vcvars)) { throw "vcvars64.bat was not found: $vcvars" }
	$environmentLines = & cmd.exe /d /s /c "`"$vcvars`" >nul && set"
	if ($LASTEXITCODE -ne 0) { throw "vcvars64.bat failed with exit code $LASTEXITCODE" }
	foreach ($line in $environmentLines) {
		$separator = $line.IndexOf('=')
		if ($separator -gt 0) {
			[Environment]::SetEnvironmentVariable($line.Substring(0, $separator), $line.Substring($separator + 1), "Process")
		}
	}
}

function Build-WithMsvc {
	Import-MsvcEnvironment
	$compiler = (Get-Command cl.exe -ErrorAction Stop).Source
	Invoke-Checked { & $compiler /nologo /std:c++20 /O2 /W4 /WX /EHsc /permissive- `
		(Join-Path $scriptDirectory "query_benchmark.cpp") `
		("/Fo:" + (Join-Path $outputPath "query_msvc.obj")) `
		("/Fe:" + (Join-Path $outputPath "query_msvc.exe")) }
	Invoke-Checked { & $compiler /nologo /std:c++20 /O2 /W4 /WX /EHsc /permissive- `
		(Join-Path $scriptDirectory "interner_benchmark.cpp") `
		("/Fo:" + (Join-Path $outputPath "interner_msvc.obj")) `
		("/Fe:" + (Join-Path $outputPath "interner_msvc.exe")) }
	Invoke-Checked { & $compiler /nologo /std:c++20 /O2 /W4 /WX /EHsc /permissive- `
		(Join-Path $scriptDirectory "benchmark_smoke.cpp") `
		(Join-Path $scriptDirectory "benchmark_harness.cpp") `
		("/Fo:" + $outputPath + "\") `
		("/Fe:" + (Join-Path $outputPath "harness_msvc.exe")) }
}

if ($Toolchain -eq "clang" -or $Toolchain -eq "all") { Build-WithClang }
if ($Toolchain -eq "msvc" -or $Toolchain -eq "all") { Build-WithMsvc }
Write-Output $outputPath
