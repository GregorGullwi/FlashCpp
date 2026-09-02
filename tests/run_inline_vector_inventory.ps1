# Static inventory guard for InlineVector spill-family attribution.
#
# Every explicit project-owned InlineVector instantiation must name a concrete
# telemetry family. Unknown attribution is forbidden; it obscures the measured
# capacity and spill behavior required by architecture boundary 1.
#
# Usage:
#   pwsh tests/run_inline_vector_inventory.ps1

param(
	[string]$Root = ""
)

$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($Root)) {
	$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
}
$Root = (Resolve-Path -LiteralPath $Root).Path

function Skip-QuotedOrComment {
	param(
		[string]$Content,
		[int]$Offset
	)

	$length = $Content.Length
	if ($Offset + 1 -lt $length -and $Content[$Offset] -eq '/' -and $Content[$Offset + 1] -eq '/') {
		$lineEnd = $Content.IndexOf("`n", $Offset + 2)
		if ($lineEnd -lt 0) {
			return $length
		}
		return $lineEnd + 1
	}
	if ($Offset + 1 -lt $length -and $Content[$Offset] -eq '/' -and $Content[$Offset + 1] -eq '*') {
		$commentEnd = $Content.IndexOf('*/', $Offset + 2, [StringComparison]::Ordinal)
		if ($commentEnd -lt 0) {
			return $length
		}
		return $commentEnd + 2
	}
	if ($Content[$Offset] -eq '"' -or $Content[$Offset] -eq "'") {
		$quote = $Content[$Offset]
		$Offset++
		while ($Offset -lt $length) {
			if ($Content[$Offset] -eq '\\') {
				$Offset += 2
				continue
			}
			$Offset++
			if ($Content[$Offset - 1] -eq $quote) {
				break
			}
		}
		return $Offset
	}
	return $Offset + 1
}

function Find-InlineVectorClose {
	param(
		[string]$Content,
		[int]$OpenOffset
	)

	$depth = 0
	$offset = $OpenOffset
	while ($offset -lt $Content.Length) {
		$character = $Content[$offset]
		if ($character -eq '/' -or $character -eq '"' -or $character -eq "'") {
			$next = Skip-QuotedOrComment -Content $Content -Offset $offset
			if ($next -ne $offset + 1) {
				$offset = $next
				continue
			}
		}
		if ($character -eq '<') {
			$depth++
		} elseif ($character -eq '>') {
			$depth--
			if ($depth -eq 0) {
				return $offset
			}
		}
		$offset++
	}
	return -1
}

$roots = @(
	(Join-Path $Root 'src'),
	(Join-Path $Root 'tests\FlashCppTest')
)
$files = @()
foreach ($sourceRoot in $roots) {
	if (-not (Test-Path -LiteralPath $sourceRoot -PathType Container)) {
		throw "Inventory source root does not exist: $sourceRoot"
	}
	$files += Get-ChildItem -LiteralPath $sourceRoot -Recurse -File |
		Where-Object { $_.Extension -in '.cpp', '.h', '.hpp' }
}

$violations = [System.Collections.Generic.List[string]]::new()
$explicitCount = 0
foreach ($file in $files) {
	$content = [IO.File]::ReadAllText($file.FullName)
	foreach ($match in [regex]::Matches($content, '\bInlineVector\s*<')) {
		$openOffset = $match.Index + $match.Length - 1
		$closeOffset = Find-InlineVectorClose -Content $content -OpenOffset $openOffset
		if ($closeOffset -lt 0) {
			throw "Unterminated InlineVector template argument list in $($file.FullName)"
		}
		$explicitCount++
		$arguments = $content.Substring($openOffset + 1, $closeOffset - $openOffset - 1)
		if ($arguments -notmatch 'InlineVectorSpillFamily::(?:OverloadResolution|TemplateArgument)') {
			$line = 1 + ([regex]::Matches($content.Substring(0, $match.Index), "`n")).Count
			$relativePath = [IO.Path]::GetRelativePath($Root, $file.FullName)
			$violations.Add("$relativePath`:$line")
		}
	}
}

Write-Host "InlineVector family inventory: explicit=$explicitCount untagged=$($violations.Count)"
if ($violations.Count -gt 0) {
	Write-Host 'RESULT: FAILED - untagged or unknown-family InlineVector instantiations:' -ForegroundColor Red
	$violations | ForEach-Object { Write-Host "  $_" -ForegroundColor Red }
	exit 1
}
Write-Host 'RESULT: OK - every explicit InlineVector names a concrete spill family'
