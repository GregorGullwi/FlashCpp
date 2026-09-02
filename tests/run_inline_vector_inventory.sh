#!/usr/bin/env bash
# Static inventory guard for InlineVector spill-family attribution.
#
# Every explicit project-owned InlineVector instantiation must name a concrete
# telemetry family. Unknown attribution is forbidden; it obscures the measured
# capacity and spill behavior required by architecture boundary 1.

set -euo pipefail

if [ "$#" -eq 0 ]; then
	repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
elif [ "$#" -eq 2 ] && [ "$1" = '--root' ]; then
	repo_root=$(cd "$2" && pwd)
else
	printf 'Usage: %s [--root <repository-root>]\n' "$0" >&2
	exit 1
fi

source_roots=("$repo_root/src" "$repo_root/tests/FlashCppTest")
for source_root in "${source_roots[@]}"; do
	if [ ! -d "$source_root" ]; then
		printf 'ERROR: Inventory source root does not exist: %s\n' "$source_root" >&2
		exit 1
	fi
done

mapfile -d '' files < <(find "${source_roots[@]}" -type f \( -name '*.cpp' -o -name '*.h' -o -name '*.hpp' \) -print0 | sort -z)
if [ "${#files[@]}" -eq 0 ]; then
	printf 'ERROR: InlineVector inventory found no source files\n' >&2
	exit 1
fi

perl -0ne '
	BEGIN { $explicit = 0; $failed = 0; }
	sub skip_non_code {
		my ($text, $offset) = @_;
		my $length = length($text);
		if (substr($text, $offset, 2) eq "//") {
			my $end = index($text, "\n", $offset + 2);
			return $end < 0 ? $length : $end + 1;
		}
		if (substr($text, $offset, 2) eq "/*") {
			my $end = index($text, "*/", $offset + 2);
			return $end < 0 ? $length : $end + 2;
		}
		my $quote = substr($text, $offset, 1);
		if ($quote eq q{"} || ord($quote) == 39) {
			$offset++;
			while ($offset < $length) {
				if (substr($text, $offset, 1) eq q{\\}) { $offset += 2; next; }
				$offset++;
				last if substr($text, $offset - 1, 1) eq $quote;
			}
			return $offset;
		}
		return $offset + 1;
	}
	sub find_template_close {
		my ($text, $offset) = @_;
		my $length = length($text);
		my $depth = 0;
		while ($offset < $length) {
			my $character = substr($text, $offset, 1);
			if ($character eq q{/} || $character eq q{"} || ord($character) == 39) {
				my $next = skip_non_code($text, $offset);
				if ($next != $offset + 1) { $offset = $next; next; }
			}
			if ($character eq q{<}) { $depth++; }
			elsif ($character eq q{>}) { $depth--; return $offset if $depth == 0; }
			$offset++;
		}
		return -1;
	}
	my $text = $_;
	my $length = length($text);
	my $offset = 0;
	while ($offset < $length) {
		my $character = substr($text, $offset, 1);
		if ($character eq q{/} || $character eq q{"} || ord($character) == 39) {
			$offset = skip_non_code($text, $offset);
			next;
		}
		if (substr($text, $offset, 12) eq "InlineVector" &&
			($offset == 0 || substr($text, $offset - 1, 1) !~ /[A-Za-z0-9_]/) &&
			($offset + 12 >= $length || substr($text, $offset + 12, 1) !~ /[A-Za-z0-9_]/)) {
			my $open = $offset + 12;
			$open++ while $open < $length && substr($text, $open, 1) =~ /\s/;
			if ($open < $length && substr($text, $open, 1) eq q{<}) {
				my $close = find_template_close($text, $open);
				if ($close < 0) { die "Unterminated InlineVector template argument list in $ARGV\n"; }
				$explicit++;
				my $arguments = substr($text, $open + 1, $close - $open - 1);
				if ($arguments !~ /InlineVectorSpillFamily::(?:OverloadResolution|TemplateArgument)/) {
					my $line = 1 + (substr($text, 0, $offset) =~ tr/\n//);
					print "$ARGV:$line\n";
					$failed = 1;
				}
				$offset = $close + 1;
				next;
			}
		}
		$offset++;
	}
	END {
		print "InlineVector family inventory: explicit=$explicit untagged=" . ($failed ? "at least one" : "0") . "\n";
		exit $failed;
	}
' "${files[@]}"

printf 'RESULT: OK - every explicit InlineVector names a concrete spill family\n'
