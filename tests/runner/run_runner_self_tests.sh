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

[ "$(runner_test_kind sample_e1001.cpp "$SCRIPT_DIR/fixtures/invalid_multi_tu/runner_self_skipped_ret0/only_support.cpp" "" "" "")" = "compile-failure" ] && encoded_kind_ok=true || encoded_kind_ok=false
assert_runner "$encoded_kind_ok" "encoded negative names are classified before main detection"

malformed_name_ok=true
for malformed_candidate in sample_e01.cpp sample_e0.cpp sample_e+1.cpp sample_e.cpp sample_e1001_e01.cpp; do
	runner_classify_negative_name "$malformed_candidate"
	[ "$RUNNER_NEGATIVE_NAME_KIND" = "malformed" ] || malformed_name_ok=false
done
assert_runner "$malformed_name_ok" "malformed diagnostic-looking terminal names are rejected"

expected_ids=$(runner_expected_diagnostic_ids sample_e1001_e1001_e1051.cpp)
if [ "$expected_ids" = $'1001\n1001\n1051' ]; then repeated_ids_ok=true; else repeated_ids_ok=false; fi
assert_runner "$repeated_ids_ok" "filename extraction preserves repeated diagnostic IDs"

if runner_linux_return_is_valid test_invalid_ret256.cpp; then range_ok=false; else range_ok=true; fi
assert_runner "$range_ok" "Linux return encodings above 255 are rejected"

diag_file="$temp_root/diag_output.txt"
printf '%s\n' \
	$(printf '\033[31m[ERROR][Parser] C:\\src\\t.cpp:3:7: error: decorated copy [SomeDiagnostic#1001]\033[0m') \
	'C:\src\t.cpp:3:7: error: plain primary [SomeDiagnostic#1001]' \
	'C:\src\t.cpp:4:2: warning: plain repeated diagnostic [OtherName#1001]' \
	'C:\src\t.cpp:5:9: error: plain note-role replacement [SomeNote#1051]' \
	"  in instantiation of 'X' requested here" \
	'[Progress] Preprocessing complete: 9 lines' > "$diag_file"
emitted_ids=$(runner_plain_emitted_diagnostic_ids "$(cat "$diag_file")")
if [ "$emitted_ids" = $'1001\n1001\n1051' ]; then
	emission_ok=true
else
	emission_ok=false
fi
assert_runner "$emission_ok" "plain diagnostics contribute only ID numbers while decorated copies are excluded"

runner_compare_diagnostic_id_multisets "$expected_ids" "$emitted_ids"
[ "$RUNNER_DIAG_COMPARISON" = "match" ] && match_ok=true || match_ok=false
assert_runner "$match_ok" "identical diagnostic ID multisets match"

runner_compare_diagnostic_id_multisets "$expected_ids" $'1001\n1051\n1051'
if [ "$RUNNER_DIAG_COMPARISON" = "mismatch" ] &&
	printf '%s' "$RUNNER_DIAG_DETAIL" | grep -qF 'missing 1001 x1' &&
	printf '%s' "$RUNNER_DIAG_DETAIL" | grep -qF 'excess 1051 x1'; then
	multiset_mismatch_ok=true
else
	multiset_mismatch_ok=false
fi
assert_runner "$multiset_mismatch_ok" "missing and excess diagnostic occurrences are counted"

runner_evaluate_negative_result sample_e1001_e1001_e1051.cpp "$RUNNER_INTERNAL_FAILURE_EXIT" no "$(cat "$diag_file")"
[ "$RUNNER_NEGATIVE_RESULT" = "bad" ] && printf '%s' "$RUNNER_NEGATIVE_DETAIL" | grep -qF 'internal failure'
internal_status_ok=$?
[ "$internal_status_ok" -eq 0 ] && internal_guard_ok=true || internal_guard_ok=false
assert_runner "$internal_guard_ok" "internal compiler status cannot pass even when expected IDs were emitted"

compatibility_name=test_constexpr_aggregate_brace_narrowing_fail.cpp
RUNNER_LEGACY_INTERNAL_COMPATIBILITY_ACTIVE_NAMES="$compatibility_name"
runner_evaluate_negative_result "$compatibility_name" "$RUNNER_INTERNAL_FAILURE_EXIT" no ""
[ "$RUNNER_NEGATIVE_RESULT" = "legacy-internal-compatibility" ] && legacy_internal_ok=true || legacy_internal_ok=false
assert_runner "$legacy_internal_ok" "a listed legacy _fail test may use the temporary internal-failure compatibility"

runner_evaluate_negative_result unlisted_legacy_fail.cpp "$RUNNER_INTERNAL_FAILURE_EXIT" no ""
[ "$RUNNER_NEGATIVE_RESULT" = "bad" ] && unlisted_internal_ok=true || unlisted_internal_ok=false
assert_runner "$unlisted_internal_ok" "an unlisted legacy _fail test cannot use internal-failure compatibility"

runner_evaluate_negative_result test_constexpr_aggregate_brace_narrowing_e1001.cpp "$RUNNER_INTERNAL_FAILURE_EXIT" no ""
[ "$RUNNER_NEGATIVE_RESULT" = "bad" ] && encoded_internal_ok=true || encoded_internal_ok=false
assert_runner "$encoded_internal_ok" "an encoded _e test cannot use legacy internal-failure compatibility"

runner_evaluate_negative_result "$compatibility_name" "$RUNNER_INTERNAL_FAILURE_EXIT" yes ""
[ "$RUNNER_NEGATIVE_RESULT" = "bad" ] && compatibility_object_ok=true || compatibility_object_ok=false
assert_runner "$compatibility_object_ok" "legacy internal-failure compatibility still forbids object output"

runner_evaluate_negative_result sample_e1001_e1001_e1051.cpp "$RUNNER_SOURCE_REJECTION_EXIT" no "$(cat "$diag_file")"
[ "$RUNNER_NEGATIVE_RESULT" = "ok" ] && clean_negative_ok=true || clean_negative_ok=false
assert_runner "$clean_negative_ok" "clean source rejection with the exact ID multiset passes"

if runner_validate_negative_names "$REPO_ROOT" &&
	runner_validate_legacy_inventory "$REPO_ROOT" "$REPO_ROOT/tests/legacy_negative_tests.txt" &&
	runner_validate_legacy_internal_compatibility \
		"$REPO_ROOT" \
		"$REPO_ROOT/tests/legacy_internal_failure_tests.txt" \
		"$REPO_ROOT/tests/legacy_negative_tests.txt"; then
	inventory_ok=true
else
	inventory_ok=false
fi
assert_runner "$inventory_ok" "the frozen legacy inventory matches exactly one current representation per entry"
[ "$RUNNER_LEGACY_INTERNAL_COMPATIBILITY_ACTIVE_COUNT" -eq 7 ] &&
	internal_compatibility_inventory_ok=true || internal_compatibility_inventory_ok=false
assert_runner "$internal_compatibility_inventory_ok" "the seven-entry legacy internal-failure compatibility inventory is active at its baseline"

saved_internal_compatibility_baseline=$RUNNER_LEGACY_INTERNAL_COMPATIBILITY_BASELINE
RUNNER_LEGACY_INTERNAL_COMPATIBILITY_BASELINE=6
if runner_validate_legacy_internal_compatibility \
	"$REPO_ROOT" \
	"$REPO_ROOT/tests/legacy_internal_failure_tests.txt" \
	"$REPO_ROOT/tests/legacy_negative_tests.txt"; then
	internal_compatibility_direction_ok=false
else
	printf '%s' "$RUNNER_LEGACY_INTERNAL_COMPATIBILITY_ERROR" | grep -qF 'above baseline 6' &&
		internal_compatibility_direction_ok=true || internal_compatibility_direction_ok=false
fi
RUNNER_LEGACY_INTERNAL_COMPATIBILITY_BASELINE=$saved_internal_compatibility_baseline
assert_runner "$internal_compatibility_direction_ok" "the compatibility count cannot rise above its directional baseline"

mkdir -p "$temp_root/inventory_repo/tests"
cp "$REPO_ROOT/tests/legacy_negative_tests.txt" "$temp_root/inventory_repo/legacy_negative_tests.txt"
touch "$temp_root/inventory_repo/tests/new_negative_fail.cpp"
if runner_validate_legacy_inventory "$temp_root/inventory_repo" "$temp_root/inventory_repo/legacy_negative_tests.txt"; then
	unknown_fail_ok=false
else
	printf '%s' "$RUNNER_INVENTORY_ERROR" | grep -qF 'unregistered legacy negative test' && unknown_fail_ok=true || unknown_fail_ok=false
fi
assert_runner "$unknown_fail_ok" "an unknown _fail.cpp name is rejected even if the inventory count would stay fixed"

cp "$REPO_ROOT/tests/legacy_negative_tests.txt" "$temp_root/mutated_inventory.txt"
printf 'X' | dd of="$temp_root/mutated_inventory.txt" bs=1 seek=0 conv=notrunc 2>/dev/null
if runner_validate_legacy_inventory "$REPO_ROOT" "$temp_root/mutated_inventory.txt"; then
	inventory_hash_ok=false
else
	printf '%s' "$RUNNER_INVENTORY_ERROR" | grep -qF 'SHA-256' && inventory_hash_ok=true || inventory_hash_ok=false
fi
assert_runner "$inventory_hash_ok" "same-count inventory mutations fail the fixed SHA-256 guard"

cp "$REPO_ROOT/tests/legacy_internal_failure_tests.txt" "$temp_root/mutated_internal_compatibility.txt"
printf 'X' | dd of="$temp_root/mutated_internal_compatibility.txt" bs=1 seek=0 conv=notrunc 2>/dev/null
if runner_validate_legacy_internal_compatibility \
	"$REPO_ROOT" \
	"$temp_root/mutated_internal_compatibility.txt" \
	"$REPO_ROOT/tests/legacy_negative_tests.txt"; then
	internal_compatibility_hash_ok=false
else
	printf '%s' "$RUNNER_LEGACY_INTERNAL_COMPATIBILITY_ERROR" | grep -qF 'SHA-256' &&
		internal_compatibility_hash_ok=true || internal_compatibility_hash_ok=false
fi
assert_runner "$internal_compatibility_hash_ok" "count-preserving compatibility inventory swaps fail the fixed SHA-256 guard"

compatibility_repo="$temp_root/compatibility_repo"
mkdir -p "$compatibility_repo/tests"
cp "$REPO_ROOT/tests/legacy_negative_tests.txt" "$compatibility_repo/legacy_negative_tests.txt"
cp "$REPO_ROOT/tests/legacy_internal_failure_tests.txt" "$compatibility_repo/legacy_internal_failure_tests.txt"
while IFS= read -r compatibility_entry; do
	touch "$compatibility_repo/tests/$compatibility_entry"
done < "$compatibility_repo/legacy_internal_failure_tests.txt"
first_compatibility_entry=$(sed -n '1p' "$compatibility_repo/legacy_internal_failure_tests.txt")
first_compatibility_stem="${first_compatibility_entry%_fail.cpp}"
rm "$compatibility_repo/tests/$first_compatibility_entry"
touch "$compatibility_repo/tests/${first_compatibility_stem}_e1001.cpp"
if runner_validate_legacy_internal_compatibility \
	"$compatibility_repo" \
	"$compatibility_repo/legacy_internal_failure_tests.txt" \
	"$compatibility_repo/legacy_negative_tests.txt" &&
	[ "$RUNNER_LEGACY_INTERNAL_COMPATIBILITY_ACTIVE_COUNT" -eq 6 ] &&
	[[ " $RUNNER_LEGACY_INTERNAL_COMPATIBILITY_ACTIVE_NAMES " != *" $first_compatibility_entry "* ]]; then
	encoded_successor_mapping_ok=true
else
	encoded_successor_mapping_ok=false
fi
assert_runner "$encoded_successor_mapping_ok" "an encoded successor satisfies historical representation but loses the compatibility exception"

rm "$compatibility_repo/tests/${first_compatibility_stem}_e1001.cpp"
if runner_validate_legacy_internal_compatibility \
	"$compatibility_repo" \
	"$compatibility_repo/legacy_internal_failure_tests.txt" \
	"$compatibility_repo/legacy_negative_tests.txt"; then
	missing_compatibility_representation_ok=false
else
	printf '%s' "$RUNNER_LEGACY_INTERNAL_COMPATIBILITY_ERROR" | grep -qF '0 current representations' &&
		missing_compatibility_representation_ok=true || missing_compatibility_representation_ok=false
fi
assert_runner "$missing_compatibility_representation_ok" "a compatibility entry with no legacy or encoded representation is rejected"

cp "$REPO_ROOT/tests/legacy_negative_tests.txt" "$compatibility_repo/missing_legacy_membership.txt"
sed -i "\\|^$first_compatibility_entry$|d" "$compatibility_repo/missing_legacy_membership.txt"
if runner_validate_legacy_internal_compatibility \
	"$REPO_ROOT" \
	"$REPO_ROOT/tests/legacy_internal_failure_tests.txt" \
	"$compatibility_repo/missing_legacy_membership.txt"; then
	compatibility_membership_ok=false
else
	printf '%s' "$RUNNER_LEGACY_INTERNAL_COMPATIBILITY_ERROR" | grep -qF 'not in the frozen legacy inventory' &&
		compatibility_membership_ok=true || compatibility_membership_ok=false
fi
assert_runner "$compatibility_membership_ok" "compatibility entries outside the frozen legacy-name inventory are rejected"

mkdir -p "$temp_root/manifest_tests"
touch "$temp_root/manifest_tests/positive_ret0.cpp"
manifest="$temp_root/expected_failures.tsv"
printf 'test\tstage\tremoval_boundary\treason\npositive_ret0.cpp\tcompile\tboundary-3\tknown compiler defect\n' > "$manifest"
if runner_validate_expected_failures "$manifest" "$temp_root/manifest_tests" &&
	[ "${RUNNER_EXPECTED_STAGE_BY_NAME[positive_ret0.cpp]}" = "compile" ]; then
	manifest_ok=true
else
	manifest_ok=false
fi
assert_runner "$manifest_ok" "a well-formed positive expected-failure manifest loads"

if runner_validate_expected_failure_schedule ""; then
	manifest_schedule_ok=true
else
	manifest_schedule_ok=false
fi
assert_runner "$manifest_schedule_ok" "a manifest entry for a scheduled regular test is accepted"

if runner_validate_expected_failure_schedule "positive_ret0.cpp"; then
	unscheduled_manifest_ok=false
else
	printf '%s' "$RUNNER_EXPECTED_FAILURE_ERROR" | grep -qF 'not a scheduled regular test' && unscheduled_manifest_ok=true || unscheduled_manifest_ok=false
fi
assert_runner "$unscheduled_manifest_ok" "manifest entries for excluded or support sources are rejected"

printf 'test\tstage\tremoval_boundary\treason\npositive_ret0.cpp\tcompile\tboundary-3\tfirst\npositive_ret0.cpp\tlink\tboundary-4\tduplicate\n' > "$manifest"
if runner_validate_expected_failures "$manifest" "$temp_root/manifest_tests"; then duplicate_manifest_ok=false; else duplicate_manifest_ok=true; fi
assert_runner "$duplicate_manifest_ok" "duplicate expected-failure rows are rejected"

printf 'test\tstage\tremoval_boundary\treason\npositive_ret0.cpp\tparse\tboundary-3\tbad stage\n' > "$manifest"
if runner_validate_expected_failures "$manifest" "$temp_root/manifest_tests"; then bad_stage_ok=false; else bad_stage_ok=true; fi
assert_runner "$bad_stage_ok" "invalid expected-failure stages are rejected"

printf 'test\tstage\tremoval_boundary\treason\nmissing.cpp\tcompile\tboundary-3\tmissing file\n' > "$manifest"
if runner_validate_expected_failures "$manifest" "$temp_root/manifest_tests"; then missing_manifest_file_ok=false; else missing_manifest_file_ok=true; fi
assert_runner "$missing_manifest_file_ok" "expected-failure rows for missing tests are rejected"

touch "$temp_root/manifest_tests/negative_e1001.cpp"
printf 'test\tstage\tremoval_boundary\treason\nnegative_e1001.cpp\tcompile\tboundary-3\tnegative test\n' > "$manifest"
if runner_validate_expected_failures "$manifest" "$temp_root/manifest_tests"; then negative_manifest_ok=false; else negative_manifest_ok=true; fi
assert_runner "$negative_manifest_ok" "negative tests cannot enter the positive expected-failure manifest"

printf 'test\tstage\tremoval_boundary\treason\npositive_ret0.cpp\tcompile\tboundary-3\n' > "$manifest"
if runner_validate_expected_failures "$manifest" "$temp_root/manifest_tests"; then malformed_manifest_ok=false; else malformed_manifest_ok=true; fi
assert_runner "$malformed_manifest_ok" "malformed expected-failure rows are rejected"

runner_evaluate_expected_stage compile success
[ "$RUNNER_EXPECTATION_RESULT" = "stale" ] && stale_success_ok=true || stale_success_ok=false
assert_runner "$stale_success_ok" "expected failure followed by success is stale"

runner_evaluate_expected_stage compile link
[ "$RUNNER_EXPECTATION_RESULT" = "stale" ] && wrong_stage_ok=true || wrong_stage_ok=false
assert_runner "$wrong_stage_ok" "a different terminal failure stage is stale"

runner_evaluate_expected_stage compile compile
[ "$RUNNER_EXPECTATION_RESULT" = "expected" ] && expected_stage_ok=true || expected_stage_ok=false
assert_runner "$expected_stage_ok" "the exact expected terminal stage matches"

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
	[ $? -eq 0 ] &&
		grep -q $'flashcpp-runner-v1\tcompatibility\tlegacy-internal-failure\tactive\tcount=7 baseline=7 selected=0 direction=down removal-boundary=2F' "$success_ci" &&
		grep -q $'flashcpp-runner-v1\tsummary\tall\tsuccess' "$success_ci" &&
		grep -q 'link mode: no-pie' "$success_output" &&
		multi_success_ok=true || multi_success_ok=false
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
