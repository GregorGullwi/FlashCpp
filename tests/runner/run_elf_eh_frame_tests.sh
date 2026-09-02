#!/usr/bin/env bash
# Regression checks for ELF exception unwinding across translation units.
# Ubuntu CI runs this after building FlashCpp; it can also be run under WSL:
#   bash tests/runner/run_elf_eh_frame_tests.sh [path/to/FlashCpp]
# The optional argument selects an existing compiler (default: x64/Sharded/FlashCpp).
# This script does not build the compiler or run the full test suite.
#
# Compile the int, double, and mixed-type multi-TU exception cases, then use
# readelf to reject per-object unwind terminators and unordered relocations.
# Link and run each case with PIE and no-PIE, in forward and reverse object order.
# Reject unwind linker diagnostics and missing .eh_frame_hdr sections, since
# a linker can succeed while silently disabling its unwind lookup table.
# Check each executable's exit status against the case's _retN suffix.
#
# Requires Linux/WSL, a Linux-targeting FlashCpp, clang++ with PIE support,
# readelf, awk, grep, and timeout. Temporary objects, executables, and logs are
# created in a fresh temporary directory and removed when the script exits.
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
compiler="${1:-$repo_root/x64/Sharded/FlashCpp}"
test_dir="$(mktemp -d)"
trap 'rm -rf -- "$test_dir"' EXIT

for case_name in throw_int_two_tu_ret52 throw_double_two_tu_ret53 eh_unwind_sections_ret0; do
	objects=()
	for source in "$repo_root/tests/multi_tu/$case_name"/*.cpp; do
		object="$test_dir/${case_name}_$(basename "${source%.cpp}").o"
		if ! "$compiler" --log-level=1 "$source" -o "$object" >"$test_dir/compile.log" 2>&1; then
			cat "$test_dir/compile.log" >&2
			exit 1
		fi
		objects+=("$object")
		readelf --debug-dump=frames "$object" >"$test_dir/frames.txt"
		if grep -q 'ZERO terminator' "$test_dir/frames.txt"; then
			echo "FAIL: $case_name/$(basename "$source") terminates a relocatable unwind section" >&2
			exit 1
		fi
		# GNU ld scans .eh_frame and its relocations together, in offset order.
		previous=-1
		while read -r offset; do
			current=$((16#$offset))
			if [ "$current" -le "$previous" ]; then
				echo "FAIL: $case_name/$(basename "$source") has unordered unwind relocations" >&2
				exit 1
			fi
			previous=$current
		done < <(readelf -rW "$object" | awk '
			/^Relocation section/ { in_eh_frame = ($3 == "\047.rela.eh_frame\047") }
			in_eh_frame && /^[0-9a-f]+ / { print $1 }')
		[ "$previous" -ge 0 ]
	done

	for mode in no-pie pie; do
		for order in forward reverse; do
			link_objects=("${objects[@]}")
			if [ "$order" = reverse ]; then
				link_objects=()
				for ((i=${#objects[@]}-1; i>=0; --i)); do
					link_objects+=("${objects[i]}")
				done
			fi
			exe="$test_dir/$case_name-$mode-$order"
			if ! clang++ "-$mode" "${link_objects[@]}" -o "$exe" >"$test_dir/link.log" 2>&1; then
				cat "$test_dir/link.log" >&2
				exit 1
			fi
			if grep -q '\.eh_frame' "$test_dir/link.log"; then
				cat "$test_dir/link.log" >&2
				exit 1
			fi
			# A successful link must retain the FDE lookup table, not silently
			# fall back to scanning only the first input object's unwind records.
			readelf -SW "$exe" >"$test_dir/sections.txt"
			grep -q '\.eh_frame_hdr' "$test_dir/sections.txt"
			status=0
			timeout 20s "$exe" || status=$?
			expected="${case_name##*_ret}"
			if [ "$status" -ne "$expected" ]; then
				echo "FAIL: $case_name ($mode, $order): expected $expected, got $status" >&2
				exit 1
			fi
			echo "PASS: $case_name ($mode, $order)"
		done
	done
done
