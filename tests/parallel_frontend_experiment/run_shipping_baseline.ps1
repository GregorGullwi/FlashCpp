param(
	[string]$Compiler = "x64/Sharded/FlashCppMSVC.exe",
	[int]$Iterations = 10,
	[int]$WarmupIterations = 1,
	[string]$OutputPath = "",
	[int]$Seed = 20260824,
	[int]$FunctionCount = 1000
)

$ErrorActionPreference = "Stop"
if ($Iterations -lt 10) {
	throw "The experiment plan requires at least 10 measured iterations."
}

$scriptDirectory = Split-Path -Parent $MyInvocation.MyCommand.Path
$repositoryRoot = Split-Path -Parent (Split-Path -Parent $scriptDirectory)
$compilerPath = [System.IO.Path]::GetFullPath((Join-Path $repositoryRoot $Compiler))
if (-not (Test-Path -LiteralPath $compilerPath)) {
	throw "Compiler not found: $compilerPath"
}

$runDirectory = Join-Path ([System.IO.Path]::GetTempPath()) ("flashcpp_parallel_frontend_{0}" -f [Guid]::NewGuid().ToString("N"))
[System.IO.Directory]::CreateDirectory($runDirectory) | Out-Null
$workloadPath = Join-Path $runDirectory "parallel_frontend_large.cpp"
& (Join-Path $scriptDirectory "generate_parallel_frontend_large.ps1") -OutputPath $workloadPath -Seed $Seed -FunctionCount $FunctionCount | Out-Null

function Get-Percentile([double[]]$Values, [double]$Fraction) {
	$ordered = @($Values | Sort-Object)
	$position = [Math]::Min($ordered.Count - 1, [Math]::Max(0, [int][Math]::Ceiling($Fraction * $ordered.Count) - 1))
	return $ordered[$position]
}

function Invoke-OneCompilation([int]$Index, [bool]$Measured) {
	Push-Location $runDirectory
	try {
		$stopwatch = [System.Diagnostics.Stopwatch]::StartNew()
		$text = (& $compilerPath --perf-stats $workloadPath 2>&1 | Out-String)
		$exitCode = $LASTEXITCODE
		$stopwatch.Stop()
	} finally {
		Pop-Location
	}
	if ($exitCode -ne 0) {
		throw "Benchmark compilation $Index failed with exit code $exitCode`n$text"
	}

	$phases = [ordered]@{}
	foreach ($phase in @("Preprocessing", "Lexer Setup", "Parsing", "Semantic Analysis", "IR Conversion", "Deferred Gen", "Code Generation", "Other", "TOTAL")) {
		$escaped = [regex]::Escape($phase)
		$match = [regex]::Match($text, "(?m)^$escaped\s*\|\s*([0-9.]+)\s*\|")
		if ($match.Success) {
			$phases[$phase] = [double]::Parse($match.Groups[1].Value, [Globalization.CultureInfo]::InvariantCulture)
		}
	}
	if (-not $phases.Contains("TOTAL")) {
		throw "Could not parse --perf-stats timing output from benchmark compilation $Index."
	}

	return [pscustomobject]@{
		iteration = $Index
		measured = $Measured
		process_wall_ms = $stopwatch.Elapsed.TotalMilliseconds
		phases_ms = $phases
	}
}

try {
	for ($index = 0; $index -lt $WarmupIterations; ++$index) {
		Invoke-OneCompilation -Index $index -Measured $false | Out-Null
	}

	$measurements = @()
	for ($index = 0; $index -lt $Iterations; ++$index) {
		$measurement = Invoke-OneCompilation -Index $index -Measured $true
		$measurements += $measurement
		$measurement | ConvertTo-Json -Compress -Depth 5
	}

	$totals = [double[]]@($measurements | ForEach-Object { $_.phases_ms.TOTAL })
	$q1 = Get-Percentile $totals 0.25
	$q3 = Get-Percentile $totals 0.75
	$processor = Get-CimInstance Win32_Processor | Select-Object -First 1
	$summary = [ordered]@{
		record_type = "summary"
		benchmark = "shipping_baseline"
		workload = "parallel_frontend_large"
		seed = $Seed
		function_count = $FunctionCount
		iterations = $Iterations
		median_ms = Get-Percentile $totals 0.5
		p95_ms = Get-Percentile $totals 0.95
		iqr_ms = $q3 - $q1
		compiler = $compilerPath
		compiler_sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $compilerPath).Hash.ToLowerInvariant()
		cpu = $processor.Name.Trim()
		physical_cores = $processor.NumberOfCores
		logical_processors = $processor.NumberOfLogicalProcessors
		os = [Environment]::OSVersion.VersionString
		unavailable_metrics = @("exclusive body parsing", "semantic dependency critical path", "constexpr exclusive time", "peak RSS")
	}
	$summaryJson = $summary | ConvertTo-Json -Compress -Depth 5
	Write-Output $summaryJson
	if ($OutputPath) {
		$resolvedOutput = [System.IO.Path]::GetFullPath((Join-Path $repositoryRoot $OutputPath))
		[System.IO.Directory]::CreateDirectory([System.IO.Path]::GetDirectoryName($resolvedOutput)) | Out-Null
		$lines = @($measurements | ForEach-Object { $_ | ConvertTo-Json -Compress -Depth 5 }) + $summaryJson
		[System.IO.File]::WriteAllLines($resolvedOutput, $lines, [System.Text.UTF8Encoding]::new($false))
	}
} finally {
	if (Test-Path -LiteralPath $runDirectory) {
		Remove-Item -LiteralPath $runDirectory -Recurse -Force
	}
}
