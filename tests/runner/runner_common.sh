#!/usr/bin/env bash

RUNNER_SOURCE_REJECTION_EXIT=1
RUNNER_INTERNAL_FAILURE_EXIT=2
# Per-test timeout policy. Values are generous because wall time inflates
# under parallel load; instant-return programs only reach them when the host
# stalls. The runtime phase additionally retries once so a transient load
# spike cannot fail a test that passes immediately afterwards.
RUNNER_COMPILE_TIMEOUT_SECONDS=120
RUNNER_RUNTIME_TIMEOUT_SECONDS=30
RUNNER_RUNTIME_TIMEOUT_RETRY_LIMIT=1
RUNNER_LEGACY_INVENTORY_COUNT=151
RUNNER_LEGACY_INVENTORY_SHA256=db998279c6ab334d45e448fe945e061b07b1b9e08fa1cd15f6fcb14aac16ed2c
RUNNER_LEGACY_INTERNAL_COMPATIBILITY_COUNT=6
RUNNER_LEGACY_INTERNAL_COMPATIBILITY_SHA256=e3ddf6167b7742c9258e36976bc6bcf200596727a1543daf101332779d4508d9
RUNNER_LEGACY_INTERNAL_COMPATIBILITY_BASELINE=7
RUNNER_LEGACY_INTERNAL_COMPATIBILITY_REMOVAL_BOUNDARY=2F

runner_relevant_sources() {
	local repo_root="$1"
	find "$repo_root/src" -type f \( -name '*.cpp' -o -name '*.h' -o -name '*.hpp' \) -print
	local relative_path
	for relative_path in FlashCpp.vcxproj FlashCppMSVC.vcxproj Makefile build_flashcpp.bat; do
		[ -f "$repo_root/$relative_path" ] && printf '%s\n' "$repo_root/$relative_path"
	done
}

runner_binary_is_fresh() {
	local binary="$1"
	local repo_root="$2"
	local source
	while IFS= read -r source; do
		[ "$source" -nt "$binary" ] && { RUNNER_NEWEST_SOURCE="$source"; return 1; }
	done < <(runner_relevant_sources "$repo_root")
	RUNNER_NEWEST_SOURCE=""
	return 0
}

runner_classify_negative_name() {
	local file_name="$1"
	local without_extension work suffix
	RUNNER_NEGATIVE_NAME_KIND='other'
	RUNNER_NEGATIVE_NAME_STEM=''
	RUNNER_EXPECTED_DIAGNOSTIC_IDS=''
	[[ "$file_name" == *.cpp ]] || return

	without_extension="${file_name%.cpp}"
	work="$without_extension"
	while [[ "$work" =~ _e([1-9][0-9]*)$ ]]; do
		suffix="_e${BASH_REMATCH[1]}"
		RUNNER_EXPECTED_DIAGNOSTIC_IDS="${BASH_REMATCH[1]}${RUNNER_EXPECTED_DIAGNOSTIC_IDS:+$'\n'$RUNNER_EXPECTED_DIAGNOSTIC_IDS}"
		work="${work%$suffix}"
	done
	if [ -n "$RUNNER_EXPECTED_DIAGNOSTIC_IDS" ] && [ -n "$work" ]; then
		if [[ "$work" =~ _e[0-9] ]] || [[ "$work" =~ _e[+-][0-9] ]] ||
			[[ "$work" =~ _e(_[A-Za-z0-9_]*)?$ ]]; then
			RUNNER_NEGATIVE_NAME_KIND='malformed'
		else
			RUNNER_NEGATIVE_NAME_KIND='encoded'
			RUNNER_NEGATIVE_NAME_STEM="$work"
		fi
		return
	fi

	if [[ "$without_extension" =~ _e[0-9] ]] ||
		[[ "$without_extension" =~ _e[+-][0-9] ]] ||
		[[ "$without_extension" =~ _e(_[A-Za-z0-9_]*)?$ ]]; then
		RUNNER_NEGATIVE_NAME_KIND='malformed'
	fi
}

runner_expected_diagnostic_ids() {
	runner_classify_negative_name "$1"
	[ "$RUNNER_NEGATIVE_NAME_KIND" = 'encoded' ] || return 1
	printf '%s\n' "$RUNNER_EXPECTED_DIAGNOSTIC_IDS"
}

runner_classify_test() {
	local file_name="$1"
	local source_path="$2"
	local platform_exclusions=" $3 "
	local support_sources=" $4 "
	local compile_only_overrides=" $5 "
	runner_classify_negative_name "$file_name"
	[ "$RUNNER_NEGATIVE_NAME_KIND" = 'malformed' ] && { RUNNER_TEST_KIND='malformed-negative'; return; }
	[ "$RUNNER_NEGATIVE_NAME_KIND" = 'encoded' ] && { RUNNER_TEST_KIND='compile-failure'; return; }
	[[ "$platform_exclusions" == *" $file_name "* ]] && { RUNNER_TEST_KIND='platform-excluded'; return; }
	[[ "$support_sources" == *" $file_name "* ]] && { RUNNER_TEST_KIND='support-source'; return; }
	[[ "$compile_only_overrides" == *" $file_name "* ]] && { RUNNER_TEST_KIND='compile-only'; return; }
	[[ "$file_name" == *_fail.cpp ]] && { RUNNER_TEST_KIND='compile-failure'; return; }
	if grep -qE '\b(int|void)[[:space:]]+main[[:space:]]*\(' "$source_path"; then
		RUNNER_TEST_KIND='runnable'
	else
		RUNNER_TEST_KIND='compile-only'
	fi
}

runner_test_kind() {
	runner_classify_test "$@"
	printf '%s' "$RUNNER_TEST_KIND"
}

runner_expected_return_value() {
	local name="$1"
	if [[ "$name" =~ _ret([0-9]+)(\.cpp)?$ ]]; then
		printf '%s' "${BASH_REMATCH[1]}"
	else
		printf '0'
	fi
}

runner_linux_return_is_valid() {
	local value
	value=$(runner_expected_return_value "$1")
	[ "$value" -le 255 ]
}

runner_pie_mode() {
	case "${FLASHCPP_PIE_MODE:-auto}" in
		supported|unsupported) printf '%s' "$FLASHCPP_PIE_MODE"; return ;;
		auto) ;;
		*) printf 'invalid'; return 2 ;;
	esac
	command -v clang++ >/dev/null 2>&1 || { printf 'unsupported'; return; }
	local probe
	probe=$(mktemp)
	if printf 'int main() { return 0; }\n' | clang++ -x c++ -fPIE -pie -o "$probe" - >/dev/null 2>&1 &&
		{ { command -v readelf >/dev/null 2>&1 && readelf -h "$probe" >/dev/null 2>&1; } ||
		  { command -v file >/dev/null 2>&1 && file "$probe" | grep -q ELF; }; }; then
		printf 'supported'
	else
		printf 'unsupported'
	fi
	rm -f "$probe"
}

runner_plain_emitted_diagnostic_ids() {
	local compiler_output="$1"
	# The structured diagnostic is also printed behind a logger prefix. Only the
	# plain rendered copy owns the filename contract, so decorated copies cannot
	# duplicate an occurrence.
	printf '%s\n' "$compiler_output" | grep -v $'^\x1b' | grep -v '^\[' |
		grep -E '^.*\[[A-Za-z][A-Za-z0-9_]*#[0-9]+\][[:space:]]*$' |
		sed -E 's,^.*\[([A-Za-z][A-Za-z0-9_]*)#([0-9]+)\][[:space:]]*$,\2,'
}

runner_compare_diagnostic_id_multisets() {
	local expected="$1"
	local emitted="$2"
	local expected_counts emitted_counts id count emitted_count
	expected_counts=$(printf '%s\n' "$expected" | awk 'NF { count[$1]++ } END { for (id in count) print id, count[id] }' | LC_ALL=C sort -n)
	emitted_counts=$(printf '%s\n' "$emitted" | awk 'NF { count[$1]++ } END { for (id in count) print id, count[id] }' | LC_ALL=C sort -n)
	RUNNER_DIAG_DETAIL=''

	while read -r id count; do
		[ -n "$id" ] || continue
		emitted_count=$(printf '%s\n' "$emitted_counts" | awk -v wanted="$id" '$1 == wanted { print $2; found=1 } END { if (!found) print 0 }')
		if [ "$count" -gt "$emitted_count" ]; then
			RUNNER_DIAG_DETAIL="${RUNNER_DIAG_DETAIL:+$RUNNER_DIAG_DETAIL; }missing $id x$((count - emitted_count))"
		fi
	done <<< "$expected_counts"
	while read -r id count; do
		[ -n "$id" ] || continue
		emitted_count=$(printf '%s\n' "$expected_counts" | awk -v wanted="$id" '$1 == wanted { print $2; found=1 } END { if (!found) print 0 }')
		if [ "$count" -gt "$emitted_count" ]; then
			RUNNER_DIAG_DETAIL="${RUNNER_DIAG_DETAIL:+$RUNNER_DIAG_DETAIL; }excess $id x$((count - emitted_count))"
		fi
	done <<< "$emitted_counts"

	if [ -z "$RUNNER_DIAG_DETAIL" ]; then
		RUNNER_DIAG_COMPARISON='match'
	else
		RUNNER_DIAG_COMPARISON='mismatch'
	fi
}

runner_evaluate_negative_result() {
	local file_name="$1"
	local compile_exit="$2"
	local object_exists="$3"
	local compiler_output="$4"
	local expected emitted
	RUNNER_NEGATIVE_RESULT='bad'
	RUNNER_NEGATIVE_DETAIL=''
	if [ "$compile_exit" -eq 124 ]; then
		RUNNER_NEGATIVE_DETAIL='compiler timed out'
		return
	fi
	if [ "$compile_exit" -gt 128 ]; then
		RUNNER_NEGATIVE_DETAIL="compiler crashed (exit: $compile_exit)"
		return
	fi
	if [ "$compile_exit" -eq "$RUNNER_INTERNAL_FAILURE_EXIT" ]; then
		runner_classify_negative_name "$file_name"
		if [[ "$file_name" == *_fail.cpp ]] &&
			[ "$RUNNER_NEGATIVE_NAME_KIND" = 'other' ] &&
			[[ " ${RUNNER_LEGACY_INTERNAL_COMPATIBILITY_ACTIVE_NAMES:-} " == *" $file_name "* ]]; then
			if [ "$object_exists" = 'yes' ]; then
				RUNNER_NEGATIVE_DETAIL='legacy internal-failure compatibility produced an object file'
				return
			fi
			RUNNER_NEGATIVE_RESULT='legacy-internal-compatibility'
			RUNNER_NEGATIVE_DETAIL="temporary compatibility through boundary $RUNNER_LEGACY_INTERNAL_COMPATIBILITY_REMOVAL_BOUNDARY"
			return
		fi
		RUNNER_NEGATIVE_DETAIL='compiler reported internal failure'
		return
	fi
	if [ "$compile_exit" -eq 0 ] && [ "$object_exists" = 'yes' ]; then
		RUNNER_NEGATIVE_DETAIL='should have failed'
		return
	fi
	if [ "$compile_exit" -ne "$RUNNER_SOURCE_REJECTION_EXIT" ] || [ "$object_exists" = 'yes' ]; then
		RUNNER_NEGATIVE_DETAIL="inconsistent compiler result (exit: $compile_exit object: $object_exists)"
		return
	fi

	runner_classify_negative_name "$file_name"
	if [ "$RUNNER_NEGATIVE_NAME_KIND" = 'encoded' ]; then
		expected=$(runner_expected_diagnostic_ids "$file_name")
		emitted=$(runner_plain_emitted_diagnostic_ids "$compiler_output")
		runner_compare_diagnostic_id_multisets "$expected" "$emitted"
		if [ "$RUNNER_DIAG_COMPARISON" != 'match' ]; then
			RUNNER_NEGATIVE_RESULT='diag-mismatch'
			RUNNER_NEGATIVE_DETAIL="$RUNNER_DIAG_DETAIL"
			return
		fi
	fi
	RUNNER_NEGATIVE_RESULT='ok'
}

runner_sha256_file() {
	local path="$1"
	if command -v sha256sum >/dev/null 2>&1; then
		sha256sum "$path" | awk '{ print $1 }'
	elif command -v shasum >/dev/null 2>&1; then
		shasum -a 256 "$path" | awk '{ print $1 }'
	else
		return 1
	fi
}

runner_validate_legacy_inventory() {
	local repo_root="$1"
	local inventory_path="$2"
	local LC_ALL=C
	local count hash previous='' name stem original candidate successor_count
	RUNNER_INVENTORY_ERROR=''
	[ -f "$inventory_path" ] || { RUNNER_INVENTORY_ERROR="legacy inventory is missing: $inventory_path"; return 1; }
	count=$(wc -l < "$inventory_path")
	[ "$count" -eq "$RUNNER_LEGACY_INVENTORY_COUNT" ] || {
		RUNNER_INVENTORY_ERROR="legacy inventory count is $count, expected $RUNNER_LEGACY_INVENTORY_COUNT"
		return 1
	}
	hash=$(runner_sha256_file "$inventory_path") || {
		RUNNER_INVENTORY_ERROR='no SHA-256 implementation is available'
		return 1
	}
	[ "$hash" = "$RUNNER_LEGACY_INVENTORY_SHA256" ] || {
		RUNNER_INVENTORY_ERROR="legacy inventory SHA-256 is $hash, expected $RUNNER_LEGACY_INVENTORY_SHA256"
		return 1
	}

	while IFS= read -r name; do
		[[ "$name" == *_fail.cpp ]] || { RUNNER_INVENTORY_ERROR="invalid legacy inventory name: $name"; return 1; }
		if [ -n "$previous" ] && [[ "$previous" > "$name" || "$previous" == "$name" ]]; then
			RUNNER_INVENTORY_ERROR="legacy inventory is not strictly sorted at: $name"
			return 1
		fi
		previous="$name"
	done < "$inventory_path"

	for original in "$repo_root"/tests/*_fail.cpp; do
		[ -f "$original" ] || continue
		name=$(basename "$original")
		grep -qxF "$name" "$inventory_path" || {
			RUNNER_INVENTORY_ERROR="unregistered legacy negative test: $name"
			return 1
		}
	done

	while IFS= read -r name; do
		original="$repo_root/tests/$name"
		stem="${name%_fail.cpp}"
		successor_count=0
		for candidate in "$repo_root/tests/$stem"_e*.cpp; do
			[ -f "$candidate" ] || continue
			runner_classify_negative_name "$(basename "$candidate")"
			if [ "$RUNNER_NEGATIVE_NAME_KIND" = 'encoded' ] && [ "$RUNNER_NEGATIVE_NAME_STEM" = "$stem" ]; then
				successor_count=$((successor_count + 1))
			fi
		done
		if [ -f "$original" ]; then
			[ "$successor_count" -eq 0 ] || {
				RUNNER_INVENTORY_ERROR="legacy test and encoded successor both exist for: $name"
				return 1
			}
		elif [ "$successor_count" -ne 1 ]; then
			RUNNER_INVENTORY_ERROR="legacy inventory entry $name has $successor_count encoded successors"
			return 1
		fi
	done < "$inventory_path"
	return 0
}

runner_validate_legacy_internal_compatibility() {
	local repo_root="$1"
	local compatibility_path="$2"
	local legacy_inventory_path="$3"
	local LC_ALL=C
	local count hash previous='' name stem original candidate successor_count
	RUNNER_LEGACY_INTERNAL_COMPATIBILITY_ERROR=''
	RUNNER_LEGACY_INTERNAL_COMPATIBILITY_ACTIVE_COUNT=0
	RUNNER_LEGACY_INTERNAL_COMPATIBILITY_ACTIVE_NAMES=''
	[ -f "$compatibility_path" ] || {
		RUNNER_LEGACY_INTERNAL_COMPATIBILITY_ERROR="legacy internal-failure compatibility inventory is missing: $compatibility_path"
		return 1
	}
	[ -f "$legacy_inventory_path" ] || {
		RUNNER_LEGACY_INTERNAL_COMPATIBILITY_ERROR="legacy negative inventory is missing: $legacy_inventory_path"
		return 1
	}
	count=$(wc -l < "$compatibility_path")
	[ "$count" -eq "$RUNNER_LEGACY_INTERNAL_COMPATIBILITY_COUNT" ] || {
		RUNNER_LEGACY_INTERNAL_COMPATIBILITY_ERROR="legacy internal-failure compatibility inventory count is $count, expected $RUNNER_LEGACY_INTERNAL_COMPATIBILITY_COUNT"
		return 1
	}
	hash=$(runner_sha256_file "$compatibility_path") || {
		RUNNER_LEGACY_INTERNAL_COMPATIBILITY_ERROR='no SHA-256 implementation is available'
		return 1
	}
	[ "$hash" = "$RUNNER_LEGACY_INTERNAL_COMPATIBILITY_SHA256" ] || {
		RUNNER_LEGACY_INTERNAL_COMPATIBILITY_ERROR="legacy internal-failure compatibility inventory SHA-256 is $hash, expected $RUNNER_LEGACY_INTERNAL_COMPATIBILITY_SHA256"
		return 1
	}

	while IFS= read -r name; do
		[[ "$name" == *_fail.cpp ]] || {
			RUNNER_LEGACY_INTERNAL_COMPATIBILITY_ERROR="invalid legacy internal-failure compatibility name: $name"
			return 1
		}
		runner_classify_negative_name "$name"
		[ "$RUNNER_NEGATIVE_NAME_KIND" = 'other' ] || {
			RUNNER_LEGACY_INTERNAL_COMPATIBILITY_ERROR="encoded or malformed name cannot enter legacy internal-failure compatibility: $name"
			return 1
		}
		if [ -n "$previous" ] && [[ "$previous" > "$name" || "$previous" == "$name" ]]; then
			RUNNER_LEGACY_INTERNAL_COMPATIBILITY_ERROR="legacy internal-failure compatibility inventory is not strictly sorted at: $name"
			return 1
		fi
		previous="$name"
		grep -qxF "$name" "$legacy_inventory_path" || {
			RUNNER_LEGACY_INTERNAL_COMPATIBILITY_ERROR="legacy internal-failure compatibility entry is not in the frozen legacy inventory: $name"
			return 1
		}

		original="$repo_root/tests/$name"
		stem="${name%_fail.cpp}"
		successor_count=0
		for candidate in "$repo_root/tests/$stem"_e*.cpp; do
			[ -f "$candidate" ] || continue
			runner_classify_negative_name "$(basename "$candidate")"
			if [ "$RUNNER_NEGATIVE_NAME_KIND" = 'encoded' ] && [ "$RUNNER_NEGATIVE_NAME_STEM" = "$stem" ]; then
				successor_count=$((successor_count + 1))
			fi
		done
		if [ -f "$original" ]; then
			[ "$successor_count" -eq 0 ] || {
				RUNNER_LEGACY_INTERNAL_COMPATIBILITY_ERROR="legacy internal-failure compatibility entry has both legacy and encoded representations: $name"
				return 1
			}
			RUNNER_LEGACY_INTERNAL_COMPATIBILITY_ACTIVE_COUNT=$((RUNNER_LEGACY_INTERNAL_COMPATIBILITY_ACTIVE_COUNT + 1))
			RUNNER_LEGACY_INTERNAL_COMPATIBILITY_ACTIVE_NAMES="${RUNNER_LEGACY_INTERNAL_COMPATIBILITY_ACTIVE_NAMES:+$RUNNER_LEGACY_INTERNAL_COMPATIBILITY_ACTIVE_NAMES }$name"
		elif [ "$successor_count" -ne 1 ]; then
			RUNNER_LEGACY_INTERNAL_COMPATIBILITY_ERROR="legacy internal-failure compatibility entry $name has $successor_count current representations"
			return 1
		fi
	done < "$compatibility_path"

	if [ "$RUNNER_LEGACY_INTERNAL_COMPATIBILITY_ACTIVE_COUNT" -gt "$RUNNER_LEGACY_INTERNAL_COMPATIBILITY_BASELINE" ]; then
		RUNNER_LEGACY_INTERNAL_COMPATIBILITY_ERROR="legacy internal-failure compatibility count is $RUNNER_LEGACY_INTERNAL_COMPATIBILITY_ACTIVE_COUNT, above baseline $RUNNER_LEGACY_INTERNAL_COMPATIBILITY_BASELINE"
		return 1
	fi
	return 0
}

runner_validate_negative_names() {
	local repo_root="$1"
	local path name
	RUNNER_NEGATIVE_NAME_ERROR=''
	for path in "$repo_root"/tests/*.cpp; do
		[ -f "$path" ] || continue
		name=$(basename "$path")
		runner_classify_negative_name "$name"
		if [ "$RUNNER_NEGATIVE_NAME_KIND" = 'malformed' ]; then
			RUNNER_NEGATIVE_NAME_ERROR="malformed diagnostic filename: $name"
			return 1
		fi
	done
	return 0
}

runner_validate_expected_failures() {
	local manifest_path="$1"
	local tests_root="$2"
	local validation_error row name stage boundary reason
	RUNNER_EXPECTED_FAILURE_ERROR=''
	declare -gA RUNNER_EXPECTED_STAGE_BY_NAME=()
	declare -gA RUNNER_EXPECTED_BOUNDARY_BY_NAME=()
	declare -gA RUNNER_EXPECTED_REASON_BY_NAME=()
	[ -f "$manifest_path" ] || { RUNNER_EXPECTED_FAILURE_ERROR="expected-failure manifest is missing: $manifest_path"; return 1; }

	validation_error=$(awk -F '\t' '
		NR == 1 {
			if ($0 != "test\tstage\tremoval_boundary\treason") {
				print "invalid expected-failure header"
				exit
			}
			next
		}
		NF != 4 {
			print "expected-failure row " NR " must contain exactly four tab-separated fields"
			exit
		}
		$1 == "" || $3 == "" || $4 == "" {
			print "expected-failure row " NR " has an empty required field"
			exit
		}
		$2 != "compile" && $2 != "link" && $2 != "run" {
			print "expected-failure row " NR " has invalid stage: " $2
			exit
		}
		seen[$1]++ {
			print "duplicate expected-failure test: " $1
			exit
		}
		$1 !~ /^[A-Za-z0-9_.+-]+\.cpp$/ {
			print "invalid expected-failure test name: " $1
			exit
		}
		$1 ~ /_fail\.cpp$/ || $1 ~ /_e[1-9][0-9]*(_e[1-9][0-9]*)*\.cpp$/ {
			print "negative test cannot enter expected-failure manifest: " $1
			exit
		}
	' "$manifest_path")
	if [ -n "$validation_error" ]; then
		RUNNER_EXPECTED_FAILURE_ERROR="$validation_error"
		return 1
	fi

	while IFS=$'\t' read -r name stage boundary reason; do
		[ -n "$name" ] || continue
		[ -f "$tests_root/$name" ] || {
			RUNNER_EXPECTED_FAILURE_ERROR="expected-failure test does not exist: $name"
			return 1
		}
		runner_classify_negative_name "$name"
		if [[ "$name" == *_fail.cpp ]] || [ "$RUNNER_NEGATIVE_NAME_KIND" != 'other' ]; then
			RUNNER_EXPECTED_FAILURE_ERROR="negative test cannot enter expected-failure manifest: $name"
			return 1
		fi
		RUNNER_EXPECTED_STAGE_BY_NAME["$name"]="$stage"
		RUNNER_EXPECTED_BOUNDARY_BY_NAME["$name"]="$boundary"
		RUNNER_EXPECTED_REASON_BY_NAME["$name"]="$reason"
	done < <(awk -F '\t' 'NR > 1 { print $1 "\t" $2 "\t" $3 "\t" $4 }' "$manifest_path")
	return 0
}

runner_validate_expected_failure_schedule() {
	local excluded_names=" $1 "
	local name
	for name in "${!RUNNER_EXPECTED_STAGE_BY_NAME[@]}"; do
		if [[ "$excluded_names" == *" $name "* ]]; then
			RUNNER_EXPECTED_FAILURE_ERROR="expected-failure test is not a scheduled regular test: $name"
			return 1
		fi
	done
	return 0
}

runner_evaluate_expected_stage() {
	local expected_stage="$1"
	local actual_stage="$2"
	RUNNER_EXPECTATION_RESULT='none'
	RUNNER_EXPECTATION_DETAIL=''
	[ -n "$expected_stage" ] || return
	case "$actual_stage" in
		compile|link|run)
			if [ "$actual_stage" = "$expected_stage" ]; then
				RUNNER_EXPECTATION_RESULT='expected'
			else
				RUNNER_EXPECTATION_RESULT='stale'
				RUNNER_EXPECTATION_DETAIL="expected $expected_stage failure, observed $actual_stage failure"
			fi
			;;
		success)
			RUNNER_EXPECTATION_RESULT='stale'
			RUNNER_EXPECTATION_DETAIL="expected $expected_stage failure, observed success"
			;;
		*)
			RUNNER_EXPECTATION_RESULT='nonwaivable'
			RUNNER_EXPECTATION_DETAIL="expected $expected_stage failure, observed non-waivable $actual_stage"
			;;
	esac
}

runner_ci_init() {
	local path="$1"
	[ -z "$path" ] && return
	mkdir -p "$(dirname "$path")"
	printf 'flashcpp-runner-v1\tmeta\tschema\t1\n' > "$path"
}

runner_ci_record() {
	local path="$1"
	shift
	local field record='flashcpp-runner-v1'
	for field in "$@"; do
		field=${field//$'\t'/ }
		field=${field//$'\r'/ }
		field=${field//$'\n'/ }
		record+=$'\t'"$field"
	done
	if [ -z "$path" ]; then
		printf '%s\n' "$record"
	else
		printf '%s\n' "$record" >> "$path"
	fi
}
