#!/usr/bin/env bash
set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
. "$SCRIPT_DIR/runner_common.sh"

failures=0
assert_runner() {
	if "$1"; then printf 'PASS: %s\n' "$2"; else printf 'FAIL: %s\n' "$2"; failures=$((failures + 1)); fi
}

temp_root=$(mktemp -d)
trap 'rm -rf "$temp_root"' EXIT
mkdir -p "$temp_root/repo/src"
printf 'binary\n' > "$temp_root/repo/FlashCpp"
printf 'source\n' > "$temp_root/repo/src/source.cpp"
touch -t 202608240100 "$temp_root/repo/FlashCpp"
touch -t 202608240101 "$temp_root/repo/src/source.cpp"
if runner_binary_is_fresh "$temp_root/repo/FlashCpp" "$temp_root/repo"; then stale_ok=false; else stale_ok=true; fi
assert_runner "$stale_ok" "stale compiler timestamps are rejected"

[ "$(runner_test_kind test_compile_only.cpp "$SCRIPT_DIR/fixtures/invalid_multi_tu/runner_self_skipped_ret0/only_support.cpp" "" "" "")" = "compile-only" ] && kind_ok=true || kind_ok=false
assert_runner "$kind_ok" "eligible sources without main are scheduled as compile-only"

[ "$(runner_test_kind test_ub_fail.cpp "$REPO_ROOT/tests/test_ub_fail.cpp" "" "" "test_ub_fail.cpp")" = "compile-only" ] && legacy_kind_ok=true || legacy_kind_ok=false
assert_runner "$legacy_kind_ok" "explicit legacy compile-only probes override the expected-failure suffix"

if runner_linux_return_is_valid test_invalid_ret256.cpp; then range_ok=false; else range_ok=true; fi
assert_runner "$range_ok" "Linux return encodings above 255 are rejected"

assertion_source='int bad;
// expected-diag: error SomeDiagnostic#1001 3:7
// expected-diag: note SomeNote#1051 4:2
// unrelated comment mentioning expected-diag without payload'
expected_keys=$(runner_expected_diagnostics "$assertion_source" | sort)
if printf '%s\n' "$expected_keys" | grep -qxF 'error SomeDiagnostic#1001 3:7' &&
	printf '%s\n' "$expected_keys" | grep -qxF 'note SomeNote#1051 4:2' &&
	[ "$(printf '%s\n' "$expected_keys" | wc -l)" -eq 2 ]; then
	extraction_ok=true
else
	extraction_ok=false
fi
assert_runner "$extraction_ok" "expected-diag comments parse to severity, ID, and location keys"

diag_file="$temp_root/diag_output.txt"
printf '%s\n' \
	$(printf '\033[31m[ERROR][Parser] C:\\src\\t.cpp:3:7: error: decorated copy [SomeDiagnostic#1001]\033[0m') \
	'C:\src\t.cpp:3:7: error: plain primary [SomeDiagnostic#1001]' \
	'C:\src\t.cpp:4:2: note: plain note [SomeNote#1051]' \
	"  in instantiation of 'X' requested here" \
	'[Progress] Preprocessing complete: 9 lines' > "$diag_file"
emitted_keys=$(runner_plain_emitted_diagnostics "$(cat "$diag_file")" | sort)
if printf '%s\n' "$emitted_keys" | grep -qxF 'error SomeDiagnostic#1001 3:7' &&
	printf '%s\n' "$emitted_keys" | grep -qxF 'note SomeNote#1051 4:2' &&
	[ "$(printf '%s\n' "$emitted_keys" | wc -l)" -eq 2 ]; then
	emission_ok=true
else
	emission_ok=false
fi
assert_runner "$emission_ok" "only plain rendered diagnostic lines are emitted candidates; decorated copies never match"

runner_compare_diagnostic_sets "$expected_keys" "$emitted_keys"
[ "$RUNNER_DIAG_COMPARISON" = "match" ] && match_ok=true || match_ok=false
assert_runner "$match_ok" "identical expected and emitted diagnostic sets match"

shifted_keys=$(printf 'error SomeDiagnostic#1001 3:8\nnote SomeNote#1051 4:2')
runner_compare_diagnostic_sets "$expected_keys" "$shifted_keys"
if [ "$RUNNER_DIAG_COMPARISON" = "mismatch" ] &&
	printf '%s' "$RUNNER_DIAG_DETAIL" | grep -qF 'missing error SomeDiagnostic#1001 3:7' &&
	printf '%s' "$RUNNER_DIAG_DETAIL" | grep -qF 'unexpected error SomeDiagnostic#1001 3:8'; then
	shift_ok=true
else
	shift_ok=false
fi
assert_runner "$shift_ok" "a location shift fails the comparison and names both sides"

runner_compare_diagnostic_sets "" "$emitted_keys"
[ "$RUNNER_DIAG_COMPARISON" = "mismatch" ] && printf '%s' "$RUNNER_DIAG_DETAIL" | grep -qF 'unexpected '
unclaimed_ok=$?
[ "$unclaimed_ok" -eq 0 ] && unclaimed_status=true || unclaimed_status=false
assert_runner "$unclaimed_status" "unclaimed emitted diagnostics fail coverage"

FLASHCPP_PIE_MODE=supported
[ "$(runner_pie_mode)" = supported ] && pie_supported_ok=true || pie_supported_ok=false
assert_runner "$pie_supported_ok" "PIE supported gate is explicit and testable"
FLASHCPP_PIE_MODE=unsupported
[ "$(runner_pie_mode)" = unsupported ] && pie_unsupported_ok=true || pie_unsupported_ok=false
assert_runner "$pie_unsupported_ok" "PIE unsupported gate is explicit and testable"
unset FLASHCPP_PIE_MODE

ci_path="$temp_root/runner.tsv"
runner_ci_init "$ci_path"
runner_ci_record "$ci_path" test fixture failed $'line one\nline two'
[ "$(wc -l < "$ci_path")" -eq 2 ] && grep -q $'^flashcpp-runner-v1\ttest\tfixture\tfailed\tline one line two$' "$ci_path" && ci_ok=true || ci_ok=false
assert_runner "$ci_ok" "CI records use the stable tab-separated schema"

if command -v clang++ >/dev/null 2>&1 && timeout --version 2>/dev/null | grep -q 'GNU coreutils'; then
	success_ci="$temp_root/success.tsv"
	success_output="$temp_root/success.out"
	bash "$REPO_ROOT/tests/run_all_tests.sh" --clang --multi-tu-root "$SCRIPT_DIR/fixtures/multi_tu_success" --ci-output "$success_ci" runner_self_multi_ret42 >"$success_output" 2>&1
	[ $? -eq 0 ] && grep -q $'flashcpp-runner-v1\tsummary\tall\tsuccess' "$success_ci" && grep -q 'link mode: no-pie' "$success_output" && multi_success_ok=true || multi_success_ok=false
	assert_runner "$multi_success_ok" "successful multi-TU fixture compiles, links, and runs"

	if [ "$(runner_pie_mode)" = "supported" ]; then
		pie_ci="$temp_root/pie.tsv"
		pie_output="$temp_root/pie.out"
		bash "$REPO_ROOT/tests/run_all_tests.sh" --clang --pie --multi-tu-root "$SCRIPT_DIR/fixtures/multi_tu_success" --ci-output "$pie_ci" runner_self_multi_ret42 >"$pie_output" 2>&1
		[ $? -eq 0 ] && grep -q $'flashcpp-runner-v1\tsummary\tall\tsuccess' "$pie_ci" && grep -q 'link mode: pie' "$pie_output" && pie_functional_ok=true || pie_functional_ok=false
		assert_runner "$pie_functional_ok" "PIE multi-TU fixture links and runs when ELF PIE is supported"
	fi

	failure_ci="$temp_root/failure.tsv"
	FLASHCPP_RERUN_PHASE=1 bash "$REPO_ROOT/tests/run_all_tests.sh" --clang --multi-tu-root "$SCRIPT_DIR/fixtures/multi_tu_failure" --ci-output "$failure_ci" runner_self_multi_link_fail_ret0 >/dev/null 2>&1
	[ $? -ne 0 ] && grep -q $'\tlink-failed\t' "$failure_ci" && multi_failure_ok=true || multi_failure_ok=false
	assert_runner "$multi_failure_ok" "failing multi-TU fixture reports a machine-readable link failure"

	invalid_ci="$temp_root/invalid.tsv"
	bash "$REPO_ROOT/tests/run_all_tests.sh" --clang --multi-tu-root "$SCRIPT_DIR/fixtures/invalid_multi_tu" --ci-output "$invalid_ci" runner_self_skipped_ret0 >/dev/null 2>&1
	[ $? -ne 0 ] && grep -q $'\tinvalid-multi-tu\t' "$invalid_ci" && skipped_ok=true || skipped_ok=false
	assert_runner "$skipped_ok" "discovered but unschedulable multi-TU sources fail discovery"

	return_ci="$temp_root/return.tsv"
	# The encoded-return preflight is Linux-only by contract (Windows exit
	# statuses are not truncated to 0-255), so its self-test must be too.
	if [ "$(uname -s 2>/dev/null)" = "Linux" ]; then
		bash "$REPO_ROOT/tests/run_all_tests.sh" --clang --multi-tu-root "$SCRIPT_DIR/fixtures/invalid_return" --ci-output "$return_ci" runner_self_ret256 >/dev/null 2>&1
		[ $? -ne 0 ] && grep -q $'\tinvalid-return\t' "$return_ci" && invalid_return_ok=true || invalid_return_ok=false
		assert_runner "$invalid_return_ok" "runner preflight rejects encoded Linux returns above 255"
	else
		printf 'SKIP: encoded-return preflight check requires Linux\n'
	fi
else
	printf 'SKIP: functional multi-TU and PIE checks require clang++ and timeout\n'
fi

[ "$failures" -eq 0 ] || exit 1
printf 'Runner self-tests passed\n'
