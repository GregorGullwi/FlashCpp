#!/bin/bash
# FlashCpp ELF Test Runner - Port of run_all_tests.ps1 for Linux
# Tests compilation and linking of all test files
# Supports parallel execution with -j N (default: number of CPU cores)
# Use --pie to select the capability-gated ELF PIE link mode.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$REPO_ROOT"
. "$SCRIPT_DIR/runner/runner_common.sh"
export -f runner_expected_return_value
export -f runner_classify_negative_name
export -f runner_expected_diagnostic_ids
export -f runner_plain_emitted_diagnostic_ids
export -f runner_compare_diagnostic_id_multisets
export -f runner_evaluate_negative_result
export RUNNER_SOURCE_REJECTION_EXIT
export RUNNER_INTERNAL_FAILURE_EXIT
export RUNNER_COMPILE_TIMEOUT_SECONDS
export RUNNER_RUNTIME_TIMEOUT_SECONDS
export RUNNER_RUNTIME_TIMEOUT_RETRY_LIMIT

# Help FlashCpp's Linux startup policy: deep template instantiation needs a
# stack well above the 8MB default. Raise the SOFT limit only (-S): setting the
# plain form would also cap the HARD limit, which clamps the compiler's own
# ensureMinimumProcessStackSize() raise back down and reintroduces crashes on
# deep chains (e.g. Chain<39> needs ~17-20MB). The compiler raises the soft
# limit itself; this only pre-warms environments where it cannot.
if ulimit -S -s >/dev/null 2>&1; then
	current_stack_kb="$(ulimit -S -s)"
	if [ "$current_stack_kb" != "unlimited" ] && [ "$current_stack_kb" -lt 16384 ]; then
		ulimit -S -s 16384 2>/dev/null || true
	fi
fi

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

# Defaults
VERBOSE=0
JOBS=$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)
REQUESTED_TEST_NAMES=()
USE_CLANG=0
CI_OUTPUT=""
MULTI_TU_ROOT="$SCRIPT_DIR/multi_tu"
LINK_MODE="no-pie"
[ "${GITHUB_ACTIONS:-}" = "true" ] && VERBOSE=1

while [ $# -gt 0 ]; do
	case "$1" in
		--verbose|-v)
			VERBOSE=1
			;;
		--clang)
			USE_CLANG=1
			;;
		--pie)
			LINK_MODE="pie"
			;;
		--ci-output)
			shift
			[ $# -gt 0 ] || { echo "ERROR: Missing value for --ci-output"; exit 1; }
			CI_OUTPUT="$1"
			;;
		--ci-output=*)
			CI_OUTPUT="${1#--ci-output=}"
			;;
		--multi-tu-root)
			shift
			[ $# -gt 0 ] || { echo "ERROR: Missing value for --multi-tu-root"; exit 1; }
			MULTI_TU_ROOT="$1"
			;;
		--multi-tu-root=*)
			MULTI_TU_ROOT="${1#--multi-tu-root=}"
			;;
		-j[0-9]*)
			JOBS="${1#-j}"
			;;
		-j|--jobs)
			opt_name="$1"
			shift
			if [ $# -eq 0 ]; then
				echo -e "${RED}ERROR:${NC} Missing value for $opt_name"
				exit 1
			fi
			JOBS="$1"
			;;
		--jobs=*)
			JOBS="${1#--jobs=}"
			;;
		--)
			shift
			while [ $# -gt 0 ]; do
				REQUESTED_TEST_NAMES+=("$(basename "$1")")
				shift
			done
			break
			;;
		*)
			REQUESTED_TEST_NAMES+=("$(basename "$1")")
			;;
	esac
	shift
done

if [ -n "$CI_OUTPUT" ] && [[ "$CI_OUTPUT" != /* ]]; then
	CI_OUTPUT="$REPO_ROOT/$CI_OUTPUT"
fi
runner_ci_init "$CI_OUTPUT"

echo "FlashCpp ELF Test Runner"
echo "========================"

FLASHCPP_BIN=""
# Clang mode bypasses FlashCpp discovery/build because it compiles tests directly.
if [ "$USE_CLANG" -eq 1 ]; then
	FLASHCPP_BIN="clang++"
elif [ -x "x64/Sharded/FlashCpp" ]; then
	FLASHCPP_BIN="./x64/Sharded/FlashCpp"
elif [ -x "x64/Debug/FlashCpp" ]; then
	FLASHCPP_BIN="./x64/Debug/FlashCpp"
else
	echo "Building..."
	make main CXX=clang++ > /dev/null 2>&1 || { echo -e "${RED}Build failed${NC}"; runner_ci_record "$CI_OUTPUT" runner compiler build-failed "make main failed"; exit 1; }
	FLASHCPP_BIN="./x64/Debug/FlashCpp"
fi
export FLASHCPP_BIN
export USE_CLANG

if [ "$USE_CLANG" -eq 0 ] && ! runner_binary_is_fresh "${FLASHCPP_BIN#./}" "$REPO_ROOT"; then
	message="Compiler binary is older than $RUNNER_NEWEST_SOURCE. Run 'make main CXX=clang++' and retry."
	echo -e "${RED}ERROR:${NC} $message"
	runner_ci_record "$CI_OUTPUT" runner compiler stale-binary "$message"
	exit 1
fi

if [ "$LINK_MODE" = "pie" ]; then
	PIE_MODE=$(runner_pie_mode)
	if [ "$PIE_MODE" = "invalid" ]; then
		message="FLASHCPP_PIE_MODE must be auto, supported, or unsupported"
		echo -e "${RED}ERROR:${NC} $message"
		runner_ci_record "$CI_OUTPUT" runner pie invalid-gate "$message"
		exit 1
	fi
	if [ "$PIE_MODE" != "supported" ]; then
		message="PIE mode was requested, but this host/linker does not support ELF PIE output"
		echo -e "${RED}ERROR:${NC} $message"
		runner_ci_record "$CI_OUTPUT" runner pie unsupported "$message"
		exit 1
	fi
fi
export LINK_MODE

print_flashcpp_build_info() {
	local version_probe_src version_probe_obj version_banner git_head binary_mtime
	if [ "$USE_CLANG" -eq 1 ]; then
		echo "Compiler: clang++"
		echo "Compiler version: $(clang++ --version | head -1)"
		return
	fi
	version_probe_src=$(mktemp /tmp/flashcpp_version_probe_XXXXXX.cpp)
	version_probe_obj=$(mktemp /tmp/flashcpp_version_probe_XXXXXX.o)
	printf 'int main() { return 0; }\n' > "$version_probe_src"
	version_banner=$("$FLASHCPP_BIN" -o "$version_probe_obj" "$version_probe_src" 2>&1 | grep -m1 'FLASHCPP VERSION' || true)
	rm -f "$version_probe_src" "$version_probe_obj"

	git_head=$(git rev-parse --short HEAD 2>/dev/null || echo "unknown")
	binary_mtime=$(date -r "${FLASHCPP_BIN#./}" '+%Y-%m-%d %H:%M:%S %z' 2>/dev/null || echo "unknown")

	[ -n "$version_banner" ] && echo "$version_banner"
	echo "FlashCpp Git HEAD: $git_head"
	echo "FlashCpp binary: $(realpath "${FLASHCPP_BIN#./}")"
	echo "FlashCpp binary mtime: $binary_mtime"
}

print_flashcpp_build_info
echo ""

if ! runner_validate_negative_names "$REPO_ROOT"; then
	echo -e "${RED}ERROR:${NC} $RUNNER_NEGATIVE_NAME_ERROR"
	runner_ci_record "$CI_OUTPUT" discovery negative-names invalid "$RUNNER_NEGATIVE_NAME_ERROR"
	exit 1
fi
if ! runner_validate_expected_failures "$REPO_ROOT/tests/expected_failures.tsv" "$REPO_ROOT/tests"; then
	echo -e "${RED}ERROR:${NC} $RUNNER_EXPECTED_FAILURE_ERROR"
	runner_ci_record "$CI_OUTPUT" discovery expected-failures invalid "$RUNNER_EXPECTED_FAILURE_ERROR"
	exit 1
fi

# Tests that require additional C helper objects for linking.
# Format: "test_file.cpp:helper_file.c" pairs, space-separated.
# The helper .c file is expected to live in the tests/ directory.
# This is exported so the parallel worker function can access it.
EXTRA_C_HELPERS="test_external_abi.cpp:test_external_abi_helper.c test_external_abi_simple.cpp:test_external_abi_simple_helper.c test_atomic_builtin_pointer_intrinsics_ret0.cpp:test_atomic_builtin_pointer_intrinsics_helper.c test_extern_var_ret42.cpp:test_extern_var_ret42_helper.c test_declspec_dllimport_var_ret42.cpp:test_declspec_dllimport_var_ret42_helper.c"
export EXTRA_C_HELPERS

contains() {
    local e match="$1"
    shift
    for e; do [[ "$e" == "$match" ]] && return 0; done
    return 1
}

# Classify every discovered source. Files without main are compile-only tests;
# they must not disappear merely because textual main detection missed them.
TEST_FILES=()
FAIL_FILES=()
COMPILE_ONLY_FILES=()
EXCLUDED_FILES=()
PLATFORM_EXCLUSIONS=""
SUPPORT_SOURCES="linux_exception_stubs.cpp"
COMPILE_ONLY_OVERRIDES=""
for candidate in tests/test_seh_*.cpp; do
	[ -f "$candidate" ] && PLATFORM_EXCLUSIONS+=" $(basename "$candidate")"
done
if ! runner_validate_expected_failure_schedule "$PLATFORM_EXCLUSIONS $SUPPORT_SOURCES"; then
	echo -e "${RED}ERROR:${NC} $RUNNER_EXPECTED_FAILURE_ERROR"
	runner_ci_record "$CI_OUTPUT" discovery expected-failures invalid "$RUNNER_EXPECTED_FAILURE_ERROR"
	exit 1
fi

DISCOVERY_FILES=()
if [ ${#REQUESTED_TEST_NAMES[@]} -gt 0 ]; then
	for requested in "${REQUESTED_TEST_NAMES[@]}"; do
		[ -f "tests/$requested" ] && DISCOVERY_FILES+=("tests/$requested")
	done
else
	for candidate in tests/*.cpp; do [ -f "$candidate" ] && DISCOVERY_FILES+=("$candidate"); done
fi

# Avoid one grep process per source. This is especially important when a WSL
# checkout lives on /mnt/c, where thousands of small cross-filesystem reads are
# much slower than one batched ripgrep/grep traversal.
USE_MAIN_CACHE=0
if [ "${BASH_VERSINFO[0]}" -ge 4 ]; then
	declare -A FILES_WITH_MAIN=()
	if [ ${#DISCOVERY_FILES[@]} -gt 0 ]; then
		if command -v rg >/dev/null 2>&1 && rg --version >/dev/null 2>&1; then
			while IFS= read -r source; do FILES_WITH_MAIN["$(basename "$source")"]=1; done < <(rg -l '\b(int|void)\s+main\s*\(' "${DISCOVERY_FILES[@]}")
		else
			while IFS= read -r source; do FILES_WITH_MAIN["$(basename "$source")"]=1; done < <(grep -lE '\b(int|void)[[:space:]]+main[[:space:]]*\(' "${DISCOVERY_FILES[@]}")
		fi
	fi
	USE_MAIN_CACHE=1
fi

DISCOVERED_ELIGIBLE=0
for f in "${DISCOVERY_FILES[@]}"; do
    base=$(basename "$f")
	if [ "$USE_MAIN_CACHE" -eq 0 ]; then
		runner_classify_test "$base" "$f" "$PLATFORM_EXCLUSIONS" "$SUPPORT_SOURCES" "$COMPILE_ONLY_OVERRIDES"
		kind="$RUNNER_TEST_KIND"
	else
		runner_classify_negative_name "$base"
		if [ "$RUNNER_NEGATIVE_NAME_KIND" = "malformed" ]; then
			kind="malformed-negative"
		elif [ "$RUNNER_NEGATIVE_NAME_KIND" = "encoded" ]; then
			kind="compile-failure"
		elif [[ " $PLATFORM_EXCLUSIONS " == *" $base "* ]]; then
		kind="platform-excluded"
		elif [[ " $SUPPORT_SOURCES " == *" $base "* ]]; then
		kind="support-source"
		elif [[ " $COMPILE_ONLY_OVERRIDES " == *" $base "* ]]; then
		kind="compile-only"
		elif [ "${FILES_WITH_MAIN[$base]+present}" = "present" ]; then
		kind="runnable"
		else
		kind="compile-only"
		fi
	fi
	case "$kind" in
		platform-excluded|support-source) EXCLUDED_FILES+=("$base:$kind") ;;
		compile-failure) FAIL_FILES+=("$base"); ((DISCOVERED_ELIGIBLE++)) ;;
		runnable) TEST_FILES+=("$base"); ((DISCOVERED_ELIGIBLE++)) ;;
		compile-only) TEST_FILES+=("$base"); COMPILE_ONLY_FILES+=("$base"); ((DISCOVERED_ELIGIBLE++)) ;;
		malformed-negative)
			message="Malformed diagnostic filename: $base"
			echo -e "${RED}ERROR:${NC} $message"
			runner_ci_record "$CI_OUTPUT" discovery "$base" invalid-negative-name "$message"
			exit 1
			;;
		*)
			message="Eligible test file was discovered but not classified: $f"
			echo -e "${RED}ERROR:${NC} $message"
			runner_ci_record "$CI_OUTPUT" discovery "$base" skipped "$message"
			exit 1
			;;
	esac
done

if [ "$DISCOVERED_ELIGIBLE" -ne "$((${#TEST_FILES[@]} + ${#FAIL_FILES[@]}))" ]; then
	message="Eligible test discovery count does not match the scheduled test count"
	echo -e "${RED}ERROR:${NC} $message"
	runner_ci_record "$CI_OUTPUT" discovery all skipped "$message"
	exit 1
fi

MULTI_TU_CASES=()
if [[ "$MULTI_TU_ROOT" != /* ]]; then MULTI_TU_ROOT="$REPO_ROOT/$MULTI_TU_ROOT"; fi
export MULTI_TU_ROOT
if [ -d "$MULTI_TU_ROOT" ]; then
	for case_dir in "$MULTI_TU_ROOT"/*; do
		[ -d "$case_dir" ] || continue
		case_name=$(basename "$case_dir")
		case_sources=()
		for source in "$case_dir"/*.cpp; do [ -f "$source" ] && case_sources+=("$source"); done
		main_count=0
		for source in "${case_sources[@]}"; do
			grep -qE '\b(int|void)[[:space:]]+main[[:space:]]*\(' "$source" && ((main_count++))
		done
		if [ ${#case_sources[@]} -eq 0 ] || [ "$main_count" -ne 1 ]; then
			message="Invalid multi-TU case '$case_name': found ${#case_sources[@]} sources and $main_count translation units containing main"
			echo -e "${RED}ERROR:${NC} $message"
			runner_ci_record "$CI_OUTPUT" discovery "$case_name" invalid-multi-tu "$message"
			exit 1
		fi
		MULTI_TU_CASES+=("$case_name")
	done
fi

if [ ${#REQUESTED_TEST_NAMES[@]} -gt 0 ]; then
	FILTERED_TEST_FILES=()
	FILTERED_FAIL_FILES=()
	for base in "${TEST_FILES[@]}"; do
		contains "$base" "${REQUESTED_TEST_NAMES[@]}" && FILTERED_TEST_FILES+=("$base")
	done
	for base in "${FAIL_FILES[@]}"; do
		contains "$base" "${REQUESTED_TEST_NAMES[@]}" && FILTERED_FAIL_FILES+=("$base")
	done
	FILTERED_MULTI_TU_CASES=()
	for case_name in "${MULTI_TU_CASES[@]}"; do
		contains "$case_name" "${REQUESTED_TEST_NAMES[@]}" && FILTERED_MULTI_TU_CASES+=("$case_name")
	done

	matched_names=()
	[ ${#FILTERED_TEST_FILES[@]} -gt 0 ] && matched_names+=("${FILTERED_TEST_FILES[@]}")
	[ ${#FILTERED_FAIL_FILES[@]} -gt 0 ] && matched_names+=("${FILTERED_FAIL_FILES[@]}")
	[ ${#FILTERED_MULTI_TU_CASES[@]} -gt 0 ] && matched_names+=("${FILTERED_MULTI_TU_CASES[@]}")
	missing_names=()
	for requested in "${REQUESTED_TEST_NAMES[@]}"; do
		if ! contains "$requested" "${matched_names[@]}" && ! contains "$requested" "${missing_names[@]}"; then
			missing_names+=("$requested")
		fi
	done

	if [ ${#missing_names[@]} -gt 0 ]; then
		echo -e "${RED}ERROR:${NC} Test file(s) not found in tests/: ${missing_names[*]}"
		for requested in "${missing_names[@]}"; do runner_ci_record "$CI_OUTPUT" discovery "$requested" not-found "requested test was not scheduled"; done
		exit 1
	fi

	TEST_FILES=("${FILTERED_TEST_FILES[@]}")
	FAIL_FILES=("${FILTERED_FAIL_FILES[@]}")
	MULTI_TU_CASES=("${FILTERED_MULTI_TU_CASES[@]}")
fi

if [ "$(uname -s 2>/dev/null)" = "Linux" ]; then
	INVALID_RETURN_NAMES=()
	for base in "${TEST_FILES[@]}"; do
		if ! contains "$base" "${COMPILE_ONLY_FILES[@]}" && ! runner_linux_return_is_valid "$base"; then
			INVALID_RETURN_NAMES+=("$base")
		fi
	done
	for case_name in "${MULTI_TU_CASES[@]}"; do
		runner_linux_return_is_valid "$case_name" || INVALID_RETURN_NAMES+=("$case_name")
	done
	if [ ${#INVALID_RETURN_NAMES[@]} -gt 0 ]; then
		for base in "${INVALID_RETURN_NAMES[@]}"; do
			value=$(runner_expected_return_value "$base")
			message="Encoded expected return value $value is outside Linux range 0-255"
			echo -e "${RED}ERROR:${NC} $base: $message"
			runner_ci_record "$CI_OUTPUT" discovery "$base" invalid-return "$message"
		done
		exit 1
	fi
fi

COMPILE_ONLY_TESTS=" ${COMPILE_ONLY_FILES[*]} "
export COMPILE_ONLY_TESTS

TOTAL=${#TEST_FILES[@]}
TOTAL_FAIL=${#FAIL_FILES[@]}
if [ ${#EXCLUDED_FILES[@]} -gt 0 ]; then
	echo "Explicitly excluded ${#EXCLUDED_FILES[@]} platform/support sources"
	if [ "$VERBOSE" = "1" ]; then printf '  %s\n' "${EXCLUDED_FILES[@]}"; fi
fi
echo "Testing $TOTAL files ($JOBS parallel jobs)..."
echo "Testing ${#MULTI_TU_CASES[@]} multi-TU cases (link mode: $LINK_MODE)..."
echo ""

# Create temp directory for results
RESULT_DIR=$(mktemp -d)
trap "rm -rf '$RESULT_DIR'" EXIT

# ──────────────────────────────────────────────────────
# Worker: test one regular file
#   Writes a single result line to $RESULT_DIR/<base>.result
#   Format:  STATUS|filename|detail
#   Workers report raw compile, link, and run outcomes. Expected-failure
#   matching happens only in central result collection.
# ──────────────────────────────────────────────────────
link_and_run_objects() {
	local base="$1"
	local result_file="$2"
	shift 2
	local objects=("$@")
	local expected_value
	expected_value=$(runner_expected_return_value "$base")
	local variant="$LINK_MODE"
	local exe link_output link_exit_code stderr_output return_value signal
	exe="/tmp/${base%.*}_$$_${variant}_exe"
	rm -f "$exe"
	local link_args=()
	if [ "$variant" = "pie" ]; then link_args+=("-pie"); else link_args+=("-no-pie"); fi
	if [ "$USE_CLANG" -eq 1 ]; then
		link_output=$(clang++ "${link_args[@]}" -o "$exe" "${objects[@]}" 2>&1)
	else
		link_output=$(clang++ "${link_args[@]}" -o "$exe" "${objects[@]}" -lstdc++ -lc 2>&1)
	fi
	link_exit_code=$?
	if [ "$link_exit_code" -gt 128 ]; then
		echo "LINKER_CRASH|$base|$variant: linker crashed (exit: $link_exit_code)" > "$result_file"
		rm -f "$exe"
		return
	fi
	if [ "$link_exit_code" -eq 126 ] || [ "$link_exit_code" -eq 127 ]; then
		echo "LINKER_DRIVER_FAIL|$base|$variant: linker could not start (exit: $link_exit_code)" > "$result_file"
		rm -f "$exe"
		return
	fi
	if [ "$link_exit_code" -eq 0 ] && [ ! -f "$exe" ]; then
		echo "LINKER_DRIVER_FAIL|$base|$variant: successful linker status produced no executable" > "$result_file"
		return
	fi
	if [ "$link_exit_code" -ne 0 ]; then
		local link_errors
		link_errors=$(echo "$link_output" | grep -E "undefined reference to|error: linker command failed|relocation.*PIE" | head -1)
		echo "LINK_FAIL|$base|$variant: $link_errors" > "$result_file"
		rm -f "$exe"
		return
	fi

	local runtime_attempts=0
	local runtime_timed_out=0
	while :; do
		stderr_output=$(timeout "$RUNNER_RUNTIME_TIMEOUT_SECONDS" "$exe" 2>&1 > /dev/null)
		return_value=$?
		runtime_attempts=$((runtime_attempts + 1))
		if [ "$return_value" -ne 124 ] || [ "$runtime_attempts" -gt "$RUNNER_RUNTIME_TIMEOUT_RETRY_LIMIT" ]; then
			[ "$return_value" -eq 124 ] && runtime_timed_out=1
			break
		fi
	done
	if [ "$runtime_timed_out" -eq 1 ]; then
		echo "RUNTIME_TIMEOUT|$base|$variant: TIMEOUT after $runtime_attempts attempts" > "$result_file"
		rm -f "$exe"
		return
	fi
	if echo "$stderr_output" | grep -qiE "(segmentation fault|illegal instruction|aborted|bus error|floating point exception|killed|dumped core|terminate called)"; then
		signal=$((return_value - 128))
		echo "RUNTIME_CRASH|$base|$variant: signal $signal" > "$result_file"
		rm -f "$exe"
		return
	fi
	if [ "$return_value" -ne "$expected_value" ]; then
		echo "RETURN_MISMATCH|$base|$variant: expected $expected_value got $return_value" > "$result_file"
		rm -f "$exe"
		return
	fi
	rm -f "$exe"
	echo "RETURN_OK|$base|$expected_value ($variant)" > "$result_file"
}
export -f link_and_run_objects

test_one_file() {
    local base="$1"
    local repo_root="$2"
    local result_dir="$3"
    local f="tests/$base"
    # Use unique per-job paths in /tmp to avoid race conditions when parallel
    # workers compile different tests that happen to share the same base name.
    local obj="/tmp/${base%.cpp}_$$.o"
    local exe="/tmp/${base%.cpp}_$$_exe"
    local result_file="$result_dir/$base.result"

    cd "$repo_root"
    rm -f "$obj" "$exe"

    # Compile (with 30 second timeout to avoid hangs)
    local extra_flags=()
    if [ "$base" == "test_no_access_control_flag_ret100.cpp" ]; then
        extra_flags+=("-fno-access-control")
    fi
    local compile_output
    if [ "$USE_CLANG" -eq 1 ]; then
        compile_output=$(timeout "$RUNNER_COMPILE_TIMEOUT_SECONDS" "$FLASHCPP_BIN" -std=c++20 -c "${extra_flags[@]}" "$f" -o "$obj" 2>&1)
    else
        compile_output=$(timeout "$RUNNER_COMPILE_TIMEOUT_SECONDS" "$FLASHCPP_BIN" --log-level=1 "${extra_flags[@]}" "$f" -o "$obj" 2>&1)
    fi
    local compile_exit=$?

    if [ "$compile_exit" -eq 124 ]; then
        echo "COMPILER_TIMEOUT|$base|compiler timed out" > "$result_file"
        rm -f "$obj"
        return
    fi
    if [ "$compile_exit" -gt 128 ]; then
        echo "COMPILER_CRASH|$base|compiler crashed (exit: $compile_exit)" > "$result_file"
        rm -f "$obj"
        return
    fi
    if [ "$compile_exit" -eq "$RUNNER_INTERNAL_FAILURE_EXIT" ]; then
        echo "COMPILER_INTERNAL|$base|compiler reported internal failure" > "$result_file"
        rm -f "$obj"
        return
    fi
    if [ "$compile_exit" -ne 0 ] && [ "$compile_exit" -ne "$RUNNER_SOURCE_REJECTION_EXIT" ]; then
        echo "COMPILER_DRIVER_FAIL|$base|compiler returned unexpected status $compile_exit" > "$result_file"
        rm -f "$obj"
        return
    fi
    if [ "$compile_exit" -eq "$RUNNER_SOURCE_REJECTION_EXIT" ]; then
        if [ -f "$obj" ]; then
            echo "COMPILER_DRIVER_FAIL|$base|source rejection produced an object file" > "$result_file"
        else
            local first_error
            first_error=$(echo "$compile_output" | grep -i "error" | head -1)
            echo "COMPILE_FAIL|$base|$first_error" > "$result_file"
        fi
        rm -f "$obj"
        return
    fi
    if [ ! -f "$obj" ]; then
        echo "COMPILER_DRIVER_FAIL|$base|successful compiler status produced no object file" > "$result_file"
        return
    fi

	if [[ "$COMPILE_ONLY_TESTS" == *" $base "* ]]; then
		echo "COMPILE_ONLY_OK|$base|no main" > "$result_file"
		rm -f "$obj"
		return
	fi

    # Compile any C helper files required for this test (from EXTRA_C_HELPERS env var)
    local extra_objs=()
    for mapping in $EXTRA_C_HELPERS; do
        local map_base="${mapping%%:*}"
        local map_helper="${mapping##*:}"
        if [ "$map_base" = "$base" ]; then
            local helper_obj="/tmp/${map_helper%.c}_$$.o"
            local helper_cflags=()
            if [ "$map_helper" = "test_atomic_builtin_pointer_intrinsics_helper.c" ]; then
                helper_cflags+=("-fno-builtin")
            fi
            clang "${helper_cflags[@]}" -c "$repo_root/tests/$map_helper" -o "$helper_obj" 2>/dev/null
            local helper_exit=$?
            if [ "$helper_exit" -ne 0 ] || [ ! -f "$helper_obj" ]; then
                echo "SUPPORT_COMPILE_FAIL|$base|failed to compile helper $map_helper" > "$result_file"
                rm -f "$obj" "$helper_obj" "${extra_objs[@]}"
                return
            fi
            extra_objs+=("$helper_obj")
        fi
    done

	link_and_run_objects "$base" "$result_file" "$obj" "${extra_objs[@]}"
    rm -f "$obj"
    rm -f "${extra_objs[@]}"
}
export -f test_one_file

test_one_multi_tu_case() {
	local case_name="$1"
	local repo_root="$2"
	local result_dir="$3"
	local case_dir="$MULTI_TU_ROOT/$case_name"
	local result_file="$result_dir/$case_name.result"
	local objects=()
	local source obj compile_output compile_exit
	for source in "$case_dir"/*.cpp; do
		[ -f "$source" ] || continue
		obj="/tmp/${case_name}_$(basename "${source%.cpp}")_$$.o"
		if [ "$USE_CLANG" -eq 1 ]; then
			compile_output=$(timeout "$RUNNER_COMPILE_TIMEOUT_SECONDS" "$FLASHCPP_BIN" -std=c++20 -c "$source" -o "$obj" 2>&1)
		else
			compile_output=$(timeout "$RUNNER_COMPILE_TIMEOUT_SECONDS" "$FLASHCPP_BIN" --log-level=1 "$source" -o "$obj" 2>&1)
		fi
		compile_exit=$?
		if [ "$compile_exit" -eq 124 ]; then
			echo "COMPILER_TIMEOUT|$case_name|$(basename "$source"): compiler timed out" > "$result_file"
			rm -f "${objects[@]}" "$obj"
			return
		fi
		if [ "$compile_exit" -gt 128 ]; then
			echo "COMPILER_CRASH|$case_name|$(basename "$source"): compiler crashed (exit: $compile_exit)" > "$result_file"
			rm -f "${objects[@]}" "$obj"
			return
		fi
		if [ "$compile_exit" -eq "$RUNNER_INTERNAL_FAILURE_EXIT" ]; then
			echo "COMPILER_INTERNAL|$case_name|$(basename "$source"): compiler reported internal failure" > "$result_file"
			rm -f "${objects[@]}" "$obj"
			return
		fi
		if [ "$compile_exit" -eq "$RUNNER_SOURCE_REJECTION_EXIT" ] && [ ! -f "$obj" ]; then
			echo "COMPILE_FAIL|$case_name|$(basename "$source"): $(echo "$compile_output" | grep -i error | head -1)" > "$result_file"
			rm -f "${objects[@]}" "$obj"
			return
		fi
		if [ "$compile_exit" -ne 0 ] || [ ! -f "$obj" ]; then
			echo "COMPILER_DRIVER_FAIL|$case_name|$(basename "$source"): inconsistent compiler result (exit: $compile_exit)" > "$result_file"
			rm -f "${objects[@]}" "$obj"
			return
		fi
		objects+=("$obj")
	done

	link_and_run_objects "$case_name" "$result_file" "${objects[@]}"
	rm -f "${objects[@]}"
}
export -f test_one_multi_tu_case

# ──────────────────────────────────────────────────────
# Worker: test one _fail file
# ──────────────────────────────────────────────────────
test_one_fail_file() {
    local base="$1"
    local repo_root="$2"
    local result_dir="$3"
    local f="tests/$base"
    local obj="/tmp/${base%.cpp}_$$.o"
    local result_file="$result_dir/$base.result"

    cd "$repo_root"
    rm -f "$obj"

    local compile_output
    if [ "$USE_CLANG" -eq 1 ]; then
        compile_output=$(timeout "$RUNNER_COMPILE_TIMEOUT_SECONDS" "$FLASHCPP_BIN" -std=c++20 -c "$f" -o "$obj" 2>&1)
    else
        compile_output=$(timeout "$RUNNER_COMPILE_TIMEOUT_SECONDS" "$FLASHCPP_BIN" --log-level=1 "$f" -o "$obj" 2>&1)
    fi
    local compile_exit=$?

    local object_exists=no
    [ -f "$obj" ] && object_exists=yes
    runner_evaluate_negative_result "$base" "$compile_exit" "$object_exists" "$compile_output"
	case "$RUNNER_NEGATIVE_RESULT" in
		ok) echo "FAIL_OK|$base|" > "$result_file" ;;
		diag-mismatch) echo "DIAG_MISMATCH|$base|$RUNNER_NEGATIVE_DETAIL" > "$result_file" ;;
        *) echo "FAIL_BAD|$base|$RUNNER_NEGATIVE_DETAIL" > "$result_file" ;;
    esac
    rm -f "$obj"
}
export -f test_one_fail_file

# ──────────────────────────────────────────────────────
# Run regular tests in parallel
# ──────────────────────────────────────────────────────
if [ ${#TEST_FILES[@]} -gt 0 ]; then
	printf '%s\n' "${TEST_FILES[@]}" | \
		xargs -P "$JOBS" -I {} bash -c 'test_one_file "$@"' _ {} "$REPO_ROOT" "$RESULT_DIR"
fi

# ──────────────────────────────────────────────────────
# Run negative tests in parallel
# ──────────────────────────────────────────────────────
if [ ${#FAIL_FILES[@]} -gt 0 ]; then
    printf '%s\n' "${FAIL_FILES[@]}" | \
        xargs -P "$JOBS" -I {} bash -c 'test_one_fail_file "$@"' _ {} "$REPO_ROOT" "$RESULT_DIR"
fi

# Multi-TU cases stay grouped and run sequentially; ordinary single-file tests
# retain their existing parallel path.
for case_name in "${MULTI_TU_CASES[@]}"; do
	test_one_multi_tu_case "$case_name" "$REPO_ROOT" "$RESULT_DIR"
done

# ──────────────────────────────────────────────────────
# Collect results
# ──────────────────────────────────────────────────────
declare -a COMPILE_OK=()
declare -a COMPILE_FAIL=()
declare -a COMPILE_FAIL_DETAILS=()
declare -a LINK_OK=()
declare -a LINK_FAIL=()
declare -a LINK_FAIL_DETAILS=()
declare -a FAIL_OK=()
declare -a FAIL_BAD=()
declare -a FAIL_BAD_DETAILS=()
declare -a RUNTIME_CRASH=()
declare -a RUNTIME_CRASH_DETAILS=()
declare -a RETURN_MISMATCH=()
declare -a RETURN_MISMATCH_DETAILS=()
declare -a EXPECTED_FAILURES=()
declare -a STALE_EXPECTATIONS=()
declare -a STALE_EXPECTATION_DETAILS=()
declare -a NONWAIVABLE_FAILURES=()
declare -a NONWAIVABLE_FAILURE_DETAILS=()
declare -a FAILED_TEST_NAMES=()

RESULT_CASES=("${TEST_FILES[@]}" "${MULTI_TU_CASES[@]}")
for base in "${RESULT_CASES[@]}"; do
    result_file="$RESULT_DIR/$base.result"
    if [ ! -f "$result_file" ]; then
        NONWAIVABLE_FAILURES+=("$base")
        NONWAIVABLE_FAILURE_DETAILS+=("missing worker result")
        FAILED_TEST_NAMES+=("$base")
        continue
    fi
    IFS='|' read -r status file detail < "$result_file"
	expected_stage=""
	if [ "${RUNNER_EXPECTED_STAGE_BY_NAME[$base]+present}" = "present" ]; then
		expected_stage="${RUNNER_EXPECTED_STAGE_BY_NAME[$base]}"
	fi
	actual_stage="$status"
	case "$status" in
		COMPILE_FAIL) actual_stage="compile" ;;
		LINK_FAIL) actual_stage="link" ;;
		RETURN_MISMATCH) actual_stage="run" ;;
		RETURN_OK|COMPILE_ONLY_OK) actual_stage="success" ;;
	esac
	runner_evaluate_expected_stage "$expected_stage" "$actual_stage"
	if [ "$RUNNER_EXPECTATION_RESULT" = "expected" ]; then
		EXPECTED_FAILURES+=("$base ($expected_stage)")
		case "$status" in
			LINK_FAIL) COMPILE_OK+=("$base") ;;
			RETURN_MISMATCH) COMPILE_OK+=("$base"); LINK_OK+=("$base") ;;
		esac
		[ "$VERBOSE" = "1" ] && echo "  $base ... OK (expected $expected_stage failure)" >&2
		continue
	fi
	if [ "$RUNNER_EXPECTATION_RESULT" = "stale" ]; then
		case "$status" in
			RETURN_OK) COMPILE_OK+=("$base"); LINK_OK+=("$base") ;;
			COMPILE_ONLY_OK) COMPILE_OK+=("$base") ;;
			LINK_FAIL) COMPILE_OK+=("$base") ;;
			RETURN_MISMATCH) COMPILE_OK+=("$base"); LINK_OK+=("$base") ;;
		esac
		STALE_EXPECTATIONS+=("$base")
		STALE_EXPECTATION_DETAILS+=("$RUNNER_EXPECTATION_DETAIL")
		FAILED_TEST_NAMES+=("$base")
		echo -e "${RED}[STALE EXPECTATION]${NC} $base ($RUNNER_EXPECTATION_DETAIL)"
		continue
	fi
    case "$status" in
        RETURN_OK)
            COMPILE_OK+=("$base")
            LINK_OK+=("$base")
            [ "$VERBOSE" = "1" ] && echo "  $base ... OK (returned ${detail})" >&2
            ;;
		COMPILE_ONLY_OK)
			COMPILE_OK+=("$base")
			[ "$VERBOSE" = "1" ] && echo "  $base ... OK (compile-only)" >&2
			;;
        RETURN_MISMATCH)
            COMPILE_OK+=("$base")
            LINK_OK+=("$base")
            RETURN_MISMATCH+=("$base")
            RETURN_MISMATCH_DETAILS+=("$detail")
            FAILED_TEST_NAMES+=("$base")
            echo -e "${RED}[RETURN MISMATCH]${NC} $base ($detail)"
            ;;
        RUNTIME_CRASH)
            COMPILE_OK+=("$base")
            LINK_OK+=("$base")
            RUNTIME_CRASH+=("$base")
            RUNTIME_CRASH_DETAILS+=("$detail")
            FAILED_TEST_NAMES+=("$base")
            echo -e "${RED}[RUNTIME CRASH]${NC} $base ($detail)"
            ;;
        LINK_FAIL)
            COMPILE_OK+=("$base")
            LINK_FAIL+=("$base")
            LINK_FAIL_DETAILS+=("$detail")
            FAILED_TEST_NAMES+=("$base")
            echo -e "${RED}[LINK FAIL]${NC} $base"
            [ -n "$detail" ] && echo "  $detail" | sed 's/^/  /'
            ;;
        COMPILE_FAIL)
            COMPILE_FAIL+=("$base")
            COMPILE_FAIL_DETAILS+=("$detail")
            FAILED_TEST_NAMES+=("$base")
            echo -e "${RED}[COMPILE FAIL]${NC} $base"
            [ -n "$detail" ] && echo "  $detail"
            ;;
		COMPILER_TIMEOUT|COMPILER_CRASH|COMPILER_INTERNAL|COMPILER_DRIVER_FAIL|SUPPORT_COMPILE_FAIL|LINKER_CRASH|LINKER_DRIVER_FAIL|RUNTIME_TIMEOUT)
			NONWAIVABLE_FAILURES+=("$base")
			NONWAIVABLE_FAILURE_DETAILS+=("$status: $detail")
			FAILED_TEST_NAMES+=("$base")
			echo -e "${RED}[NON-WAIVABLE FAILURE]${NC} $base ($status: $detail)"
			;;
		*)
			NONWAIVABLE_FAILURES+=("$base")
			NONWAIVABLE_FAILURE_DETAILS+=("unknown worker status $status: $detail")
			FAILED_TEST_NAMES+=("$base")
			echo -e "${RED}[NON-WAIVABLE FAILURE]${NC} $base (unknown worker status $status)"
            ;;
    esac
done

for base in "${FAIL_FILES[@]}"; do
    result_file="$RESULT_DIR/$base.result"
    if [ ! -f "$result_file" ]; then
        FAIL_BAD+=("$base (no result)")
        FAIL_BAD_DETAILS+=("")
        FAILED_TEST_NAMES+=("$base")
        continue
    fi
    IFS='|' read -r status file detail < "$result_file"
    case "$status" in
        FAIL_OK)
            FAIL_OK+=("$base")
            [ "$VERBOSE" = "1" ] && echo "  $base ... OK (failed as expected)" >&2
            ;;
        FAIL_BAD)
            FAIL_BAD+=("$base")
            FAIL_BAD_DETAILS+=("$detail")
            FAILED_TEST_NAMES+=("$base")
            echo -e "${RED}[NEGATIVE CONTRACT FAILURE]${NC} $base ($detail)"
            ;;
        DIAG_MISMATCH)
            FAIL_BAD+=("$base")
            FAIL_BAD_DETAILS+=("$detail")
            FAILED_TEST_NAMES+=("$base")
            echo -e "${RED}[DIAGNOSTIC MISMATCH]${NC} $base ($detail)"
            ;;
    esac
done

# Summary
echo ""
echo "========================"
echo "SUMMARY"
echo "========================"
echo "Total: $TOTAL single-file tests and ${#MULTI_TU_CASES[@]} multi-TU cases (with $JOBS parallel jobs)"
printf "Compile: ${GREEN}%d pass${NC} / ${RED}%d fail${NC}\n" "${#COMPILE_OK[@]}" "${#COMPILE_FAIL[@]}"
printf "Link:    ${GREEN}%d pass${NC} / ${RED}%d fail${NC}\n" "${#LINK_OK[@]}" "${#LINK_FAIL[@]}"
printf "Runtime: ${GREEN}%d pass${NC} / ${RED}%d crash${NC} / ${RED}%d mismatch${NC}\n" "$((${#LINK_OK[@]} - ${#RUNTIME_CRASH[@]} - ${#RETURN_MISMATCH[@]}))" "${#RUNTIME_CRASH[@]}" "${#RETURN_MISMATCH[@]}"
[ ${#FAIL_FILES[@]} -gt 0 ] && printf "Negative: ${GREEN}%d correct${NC} / ${RED}%d wrong${NC}\n" "${#FAIL_OK[@]}" "${#FAIL_BAD[@]}"
[ ${#EXPECTED_FAILURES[@]} -gt 0 ] && printf "Expected positive failures: ${GREEN}%d matched${NC}\n" "${#EXPECTED_FAILURES[@]}"

# Show failures with details
if [ ${#COMPILE_FAIL[@]} -gt 0 ]; then
    echo -e "\n${RED}Compile failures (${#COMPILE_FAIL[@]}):${NC}"
    local_limit=${#COMPILE_FAIL[@]}
    [ "$USE_CLANG" -eq 0 ] && local_limit=20
    for i in "${!COMPILE_FAIL[@]}"; do
        [ "$i" -ge "$local_limit" ] && break
        if [ -n "${COMPILE_FAIL_DETAILS[$i]}" ]; then
            echo "  ${COMPILE_FAIL[$i]} — ${COMPILE_FAIL_DETAILS[$i]}"
        else
            echo "  ${COMPILE_FAIL[$i]}"
        fi
    done
    [ ${#COMPILE_FAIL[@]} -gt $local_limit ] && echo "  ... and $((${#COMPILE_FAIL[@]} - local_limit)) more"
fi

if [ ${#LINK_FAIL[@]} -gt 0 ]; then
    echo -e "\n${YELLOW}Link failures (${#LINK_FAIL[@]}) - likely need C++ features:${NC}"
    echo "  (vtables, constructors, exceptions, etc.)"
    local_limit=${#LINK_FAIL[@]}
    [ "$USE_CLANG" -eq 0 ] && local_limit=20
    for i in "${!LINK_FAIL[@]}"; do
        [ "$i" -ge "$local_limit" ] && break
        if [ -n "${LINK_FAIL_DETAILS[$i]}" ]; then
            echo "  ${LINK_FAIL[$i]} — ${LINK_FAIL_DETAILS[$i]}"
        else
            echo "  ${LINK_FAIL[$i]}"
        fi
    done
    [ ${#LINK_FAIL[@]} -gt $local_limit ] && echo "  ... and $((${#LINK_FAIL[@]} - local_limit)) more"
fi

if [ ${#RUNTIME_CRASH[@]} -gt 0 ]; then
    echo -e "\n${RED}Runtime crashes (${#RUNTIME_CRASH[@]}):${NC}"
    for i in "${!RUNTIME_CRASH[@]}"; do
        if [ -n "${RUNTIME_CRASH_DETAILS[$i]}" ]; then
            echo "  ${RUNTIME_CRASH[$i]} — ${RUNTIME_CRASH_DETAILS[$i]}"
        else
            echo "  ${RUNTIME_CRASH[$i]}"
        fi
    done
fi

if [ ${#RETURN_MISMATCH[@]} -gt 0 ]; then
    echo -e "\n${RED}Return value mismatches (${#RETURN_MISMATCH[@]}):${NC}"
    for i in "${!RETURN_MISMATCH[@]}"; do
        if [ -n "${RETURN_MISMATCH_DETAILS[$i]}" ]; then
            echo "  ${RETURN_MISMATCH[$i]} — ${RETURN_MISMATCH_DETAILS[$i]}"
        else
            echo "  ${RETURN_MISMATCH[$i]}"
        fi
    done
fi

[ ${#STALE_EXPECTATIONS[@]} -gt 0 ] && {
	echo -e "\n${RED}Stale expected failures:${NC}"
	for i in "${!STALE_EXPECTATIONS[@]}"; do
		echo "  ${STALE_EXPECTATIONS[$i]} — ${STALE_EXPECTATION_DETAILS[$i]}"
	done
}

[ ${#NONWAIVABLE_FAILURES[@]} -gt 0 ] && {
	echo -e "\n${RED}Non-waivable runner/compiler failures:${NC}"
	for i in "${!NONWAIVABLE_FAILURES[@]}"; do
		echo "  ${NONWAIVABLE_FAILURES[$i]} — ${NONWAIVABLE_FAILURE_DETAILS[$i]}"
	done
}

[ ${#FAIL_BAD[@]} -gt 0 ] && {
    echo -e "\n${RED}Negative tests that failed their contract:${NC}"
    for i in "${!FAIL_BAD[@]}"; do
        if [ -n "${FAIL_BAD_DETAILS[$i]}" ]; then
            echo "  ${FAIL_BAD[$i]} — ${FAIL_BAD_DETAILS[$i]}"
        else
            echo "  ${FAIL_BAD[$i]}"
        fi
    done
}

echo ""
if [ ${#COMPILE_FAIL[@]} -eq 0 ] && [ ${#LINK_FAIL[@]} -eq 0 ] && [ ${#FAIL_BAD[@]} -eq 0 ] &&
	[ ${#RETURN_MISMATCH[@]} -eq 0 ] && [ ${#RUNTIME_CRASH[@]} -eq 0 ] &&
	[ ${#STALE_EXPECTATIONS[@]} -eq 0 ] && [ ${#NONWAIVABLE_FAILURES[@]} -eq 0 ]; then
    echo -e "${GREEN}RESULT: SUCCESS${NC}"
	runner_ci_record "$CI_OUTPUT" summary all success "single=$TOTAL multi-tu=${#MULTI_TU_CASES[@]} link=$LINK_MODE"
    exit 0
else
	for name in "${COMPILE_FAIL[@]}"; do runner_ci_record "$CI_OUTPUT" test "$name" compile-failed ""; done
	for name in "${LINK_FAIL[@]}"; do runner_ci_record "$CI_OUTPUT" test "$name" link-failed ""; done
	for name in "${FAIL_BAD[@]}"; do runner_ci_record "$CI_OUTPUT" test "$name" negative-contract-failed ""; done
	for name in "${RETURN_MISMATCH[@]}"; do runner_ci_record "$CI_OUTPUT" test "$name" return-mismatch ""; done
	for name in "${RUNTIME_CRASH[@]}"; do runner_ci_record "$CI_OUTPUT" test "$name" runtime-crash ""; done
	for name in "${STALE_EXPECTATIONS[@]}"; do runner_ci_record "$CI_OUTPUT" test "$name" stale-expectation ""; done
	for name in "${NONWAIVABLE_FAILURES[@]}"; do runner_ci_record "$CI_OUTPUT" test "$name" non-waivable-failure ""; done
	runner_ci_record "$CI_OUTPUT" summary all failed "single=$TOTAL multi-tu=${#MULTI_TU_CASES[@]} link=$LINK_MODE"
    if [ "${FLASHCPP_RERUN_PHASE:-0}" != "1" ] && [ ${#FAILED_TEST_NAMES[@]} -gt 0 ]; then
        echo "Re-running failing tests sequentially for diagnostics..."
        echo ""
        rerun_args=(-j1 -v)
        [ "$USE_CLANG" -eq 1 ] && rerun_args+=(--clang)
		[ "$LINK_MODE" = "pie" ] && rerun_args+=(--pie)
		rerun_args+=(--multi-tu-root "$MULTI_TU_ROOT")
        FLASHCPP_RERUN_PHASE=1 bash "$0" "${rerun_args[@]}" "${FAILED_TEST_NAMES[@]}"
        echo ""
    fi
    echo -e "${RED}RESULT: FAILED${NC}"
    exit 1
fi
