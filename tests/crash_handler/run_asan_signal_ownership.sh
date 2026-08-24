#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "$script_dir/../.." && pwd)"
test_dir="$(mktemp -d)"
trap 'rm -rf -- "$test_dir"' EXIT

clang++ \
	-std=c++20 \
	-fsanitize=address \
	-fno-omit-frame-pointer \
	-I"$repo_root/src" \
	"$script_dir/asan_signal_ownership.cpp" \
	-o "$test_dir/asan_signal_ownership"

clang++ \
	-std=c++20 \
	-I"$repo_root/src" \
	"$script_dir/asan_signal_ownership.cpp" \
	-o "$test_dir/native_signal_ownership"

set +e
{
	timeout 20s "$test_dir/native_signal_ownership" raise
} >"$test_dir/native-output.txt" 2>&1
native_status=$?
set -e
native_output="$(<"$test_dir/native-output.txt")"

if [ "$native_status" -eq 0 ]; then
	echo "Native crash-handler ownership probe unexpectedly exited successfully" >&2
	exit 1
fi
if ! grep -q "FLASHCPP CRASHED" <<<"$native_output"; then
	echo "FlashCpp's native crash handler did not report the fatal signal" >&2
	echo "$native_output" >&2
	exit 1
fi

set +e
(
	ulimit -S -s 1024
	ASAN_OPTIONS=abort_on_error=1:halt_on_error=1:symbolize=1:fast_unwind_on_fatal=1 \
		timeout 20s "$test_dir/asan_signal_ownership"
) >"$test_dir/output.txt" 2>&1
status=$?
set -e
output="$(<"$test_dir/output.txt")"

if [ "$status" -eq 0 ]; then
	echo "ASAN crash-handler ownership probe unexpectedly exited successfully" >&2
	exit 1
fi
if ! grep -q "AddressSanitizer: stack-overflow" <<<"$output"; then
	echo "ASAN did not report the stack overflow" >&2
	echo "$output" >&2
	exit 1
fi
if grep -q "FLASHCPP CRASHED" <<<"$output"; then
	echo "FlashCpp's crash handler masked ASAN's fatal-signal report" >&2
	exit 1
fi

echo "ASAN retains fatal-signal ownership"
