#!/bin/bash
# FlashCpp ELF Test Runner - Port of run_all_tests.ps1 for Linux
# Tests compilation and linking of all test files
# Supports parallel execution with -j N (default: number of CPU cores)

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$REPO_ROOT"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

# Defaults
VERBOSE=0
JOBS=$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)
REQUESTED_TEST_NAMES=()
[ "${GITHUB_ACTIONS:-}" = "true" ] && VERBOSE=1

while [ $# -gt 0 ]; do
	case "$1" in
		--verbose|-v)
			VERBOSE=1
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

echo "FlashCpp ELF Test Runner"
echo "========================"

# Build if needed
if [ ! -f "x64/Debug/FlashCpp" ]; then
    echo "Building..."
    make main CXX=clang++ > /dev/null 2>&1 || { echo -e "${RED}Build failed${NC}"; exit 1; }
fi

# Expected failures
EXPECTED_FAIL=(
    # Standard header tests have been moved to tests/std/
    # They are no longer run by this script to avoid timeouts and failures
    # See tests/std/STANDARD_HEADERS_MISSING_FEATURES.md for detailed analysis
)

# Expected link failures - files that compile but require external C helper files
# Note: test_dynamic_cast_debug_ret10.cpp and test_abstract_class_ret98.cpp now work - RTTI support has been implemented
# Note: test_virtual_inheritance.cpp, test_covariant_return.cpp, test_varargs.cpp link successfully but may have runtime issues
# Note: test_placement_new_parsing_ret42.cpp now compiles and links successfully with array initializer support
EXPECTED_LINK_FAIL=(
)

# Tests that require additional C helper objects for linking.
# Format: "test_file.cpp:helper_file.c" pairs, space-separated.
# The helper .c file is expected to live in the tests/ directory.
# This is exported so the parallel worker function can access it.
EXTRA_C_HELPERS="test_external_abi.cpp:test_external_abi_helper.c test_external_abi_simple.cpp:test_external_abi_helper.c"
export EXTRA_C_HELPERS

# Expected runtime crashes - files that compile and link but crash at runtime
EXPECTED_RUNTIME_CRASH=(
)

contains() {
    local e match="$1"
    shift
    for e; do [[ "$e" == "$match" ]] && return 0; done
    return 1
}

# Get test files with main()
TEST_FILES=()
FAIL_FILES=()
SEH_SKIPPED=0
for f in tests/*.cpp; do
    [ -f "$f" ] || continue
    base=$(basename "$f")

    # Skip Windows-only SEH tests on Linux
    if [[ "$base" == test_seh_*.cpp ]]; then
        ((SEH_SKIPPED++))
        continue
    fi

    if grep -q '\bint\s\+main\s*(' "$f" || grep -q '\bvoid\s\+main\s*(' "$f"; then
        [[ "$base" == *"_fail.cpp" ]] && FAIL_FILES+=("$base") || TEST_FILES+=("$base")
    fi
done

if [ ${#REQUESTED_TEST_NAMES[@]} -gt 0 ]; then
	FILTERED_TEST_FILES=()
	FILTERED_FAIL_FILES=()
	for base in "${TEST_FILES[@]}"; do
		contains "$base" "${REQUESTED_TEST_NAMES[@]}" && FILTERED_TEST_FILES+=("$base")
	done
	for base in "${FAIL_FILES[@]}"; do
		contains "$base" "${REQUESTED_TEST_NAMES[@]}" && FILTERED_FAIL_FILES+=("$base")
	done

	matched_names=()
	[ ${#FILTERED_TEST_FILES[@]} -gt 0 ] && matched_names+=("${FILTERED_TEST_FILES[@]}")
	[ ${#FILTERED_FAIL_FILES[@]} -gt 0 ] && matched_names+=("${FILTERED_FAIL_FILES[@]}")
	missing_names=()
	for requested in "${REQUESTED_TEST_NAMES[@]}"; do
		if ! contains "$requested" "${matched_names[@]}" && ! contains "$requested" "${missing_names[@]}"; then
			missing_names+=("$requested")
		fi
	done

	if [ ${#missing_names[@]} -gt 0 ]; then
		echo -e "${RED}ERROR:${NC} Test file(s) not found in tests/: ${missing_names[*]}"
		exit 1
	fi

	TEST_FILES=("${FILTERED_TEST_FILES[@]}")
	FAIL_FILES=("${FILTERED_FAIL_FILES[@]}")
fi

TOTAL=${#TEST_FILES[@]}
TOTAL_FAIL=${#FAIL_FILES[@]}
[ "$SEH_SKIPPED" -gt 0 ] && echo "Skipped $SEH_SKIPPED Windows-only SEH tests"
echo "Testing $TOTAL files ($JOBS parallel jobs)..."
echo ""

# Create temp directory for results
RESULT_DIR=$(mktemp -d)
trap "rm -rf '$RESULT_DIR'" EXIT

# ──────────────────────────────────────────────────────
# Worker: test one regular file
#   Writes a single result line to $RESULT_DIR/<base>.result
#   Format:  STATUS|filename|detail
#   STATUS: COMPILE_OK, COMPILE_FAIL, LINK_OK, LINK_FAIL,
#           RUNTIME_CRASH, RETURN_MISMATCH, RETURN_OK, EXPECTED_LINK_FAIL, EXPECTED_FAIL
# ──────────────────────────────────────────────────────
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
    compile_output=$(timeout 30 ./x64/Debug/FlashCpp --log-level=1 "${extra_flags[@]}" -o "$obj" "$f" 2>&1)
    local compile_exit=$?

    # Check if compiler crashed (any signal kill returns 128+signal; e.g. 134=SIGABRT,
    # 135=SIGBUS, 136=SIGFPE, 137=SIGKILL, 139=SIGSEGV, etc.)
    if [ $compile_exit -gt 128 ]; then
        echo "COMPILE_FAIL|$base|CRASHED (exit: $compile_exit)" > "$result_file"
        rm -f "$obj"
        return
    fi

    if [ -f "$obj" ]; then
        # Compile any C helper files required for this test (from EXTRA_C_HELPERS env var)
        local extra_objs=()
        for mapping in $EXTRA_C_HELPERS; do
            local map_base="${mapping%%:*}"
            local map_helper="${mapping##*:}"
            if [ "$map_base" = "$base" ]; then
                local helper_obj="/tmp/${map_helper%.c}_$$.o"
                clang -c "$repo_root/tests/$map_helper" -o "$helper_obj" 2>/dev/null
                extra_objs+=("$helper_obj")
            fi
        done

        # Link
        local link_output
        link_output=$(clang++ -no-pie -o "$exe" "$obj" "${extra_objs[@]}" -lstdc++ -lc 2>&1)
        local link_exit_code=$?

        if [ $link_exit_code -eq 0 ]; then
            # Run the executable to validate return value
            local stderr_output
            stderr_output=$(timeout 5 "$exe" 2>&1 > /dev/null)
            local return_value=$?

            # Check for timeout (exit code 124)
            if [ $return_value -eq 124 ]; then
                echo "RUNTIME_CRASH|$base|TIMEOUT" > "$result_file"
                rm -f "$exe" "$obj"
                return
            fi

            # Check for actual crashes by looking for crash indicators in stderr
            if echo "$stderr_output" | grep -qiE "(segmentation fault|illegal instruction|aborted|bus error|floating point exception|killed|dumped core|terminate called)"; then
                local signal=$((return_value - 128))
                echo "RUNTIME_CRASH|$base|signal $signal" > "$result_file"
                rm -f "$exe" "$obj"
                return
            fi

            # Check if the filename indicates an expected return value
            if [[ "$base" =~ _ret([0-9]+)\.cpp$ ]]; then
                local expected_value="${BASH_REMATCH[1]}"
                if [ "$return_value" -ne "$expected_value" ]; then
                    echo "RETURN_MISMATCH|$base|expected $expected_value got $return_value" > "$result_file"
                else
                    echo "RETURN_OK|$base|$expected_value" > "$result_file"
                fi
            else
                echo "RETURN_OK|$base|$return_value" > "$result_file"
            fi
            rm -f "$exe"
        else
            # Link failure
            local link_errors
            link_errors=$(echo "$link_output" | grep -E "undefined reference to|error: linker command failed" | head -5)
            echo "LINK_FAIL|$base|$(echo "$link_errors" | head -1)" > "$result_file"
        fi
    else
        local first_error
        first_error=$(echo "$compile_output" | grep -i "error" | head -1)
        echo "COMPILE_FAIL|$base|$first_error" > "$result_file"
    fi
    rm -f "$obj"
    rm -f "${extra_objs[@]}"
}
export -f test_one_file

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
    compile_output=$(timeout 30 ./x64/Debug/FlashCpp --log-level=1 -o "$obj" "$f" 2>&1)
    local compile_exit=$?

    # Check if compiler crashed (any signal kill returns 128+signal; e.g. 134=SIGABRT,
    # 135=SIGBUS, 136=SIGFPE, 137=SIGKILL, 139=SIGSEGV, etc.)
    # A crash must be treated as a failure even for _fail tests — the compiler is
    # expected to report a clean compile error, not to crash.
    if [ $compile_exit -gt 128 ]; then
        echo "FAIL_BAD|$base|CRASHED (exit: $compile_exit)" > "$result_file"
        rm -f "$obj"
        return
    fi

    if [ -f "$obj" ]; then
        echo "FAIL_BAD|$base|should have failed" > "$result_file"
        rm -f "$obj"
    else
        echo "FAIL_OK|$base|" > "$result_file"
    fi
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
# Run _fail tests in parallel
# ──────────────────────────────────────────────────────
if [ ${#FAIL_FILES[@]} -gt 0 ]; then
    printf '%s\n' "${FAIL_FILES[@]}" | \
        xargs -P "$JOBS" -I {} bash -c 'test_one_fail_file "$@"' _ {} "$REPO_ROOT" "$RESULT_DIR"
fi

# ──────────────────────────────────────────────────────
# Collect results
# ──────────────────────────────────────────────────────
declare -a COMPILE_OK=()
declare -a COMPILE_FAIL=()
declare -a LINK_OK=()
declare -a LINK_FAIL=()
declare -a FAIL_OK=()
declare -a FAIL_BAD=()
declare -a RUNTIME_CRASH=()
declare -a RETURN_MISMATCH=()

for base in "${TEST_FILES[@]}"; do
    result_file="$RESULT_DIR/$base.result"
    if [ ! -f "$result_file" ]; then
        COMPILE_FAIL+=("$base (no result)")
        continue
    fi
    IFS='|' read -r status file detail < "$result_file"
    case "$status" in
        RETURN_OK)
            COMPILE_OK+=("$base")
            LINK_OK+=("$base")
            [ "$VERBOSE" = "1" ] && echo "  $base ... OK (returned ${detail})" >&2
            ;;
        RETURN_MISMATCH)
            COMPILE_OK+=("$base")
            LINK_OK+=("$base")
            RETURN_MISMATCH+=("$base")
            echo -e "${RED}[RETURN MISMATCH]${NC} $base ($detail)"
            ;;
        RUNTIME_CRASH)
            COMPILE_OK+=("$base")
            LINK_OK+=("$base")
            # Check if this is an expected runtime crash
            if contains "$base" "${EXPECTED_RUNTIME_CRASH[@]}"; then
                [ "$VERBOSE" = "1" ] && echo "  $base ... OK (expected runtime crash)" >&2
            else
                RUNTIME_CRASH+=("$base")
                echo -e "${RED}[RUNTIME CRASH]${NC} $base ($detail)"
            fi
            ;;
        LINK_FAIL)
            COMPILE_OK+=("$base")
            # Check if this is an expected link failure
            if contains "$base" "${EXPECTED_LINK_FAIL[@]}"; then
                LINK_OK+=("$base")
                [ "$VERBOSE" = "1" ] && echo "  $base ... OK (expected link fail)" >&2
            else
                LINK_FAIL+=("$base")
                echo -e "${RED}[LINK FAIL]${NC} $base"
                [ -n "$detail" ] && echo "  $detail" | sed 's/^/  /'
            fi
            ;;
        COMPILE_FAIL)
            # Check if this is an expected failure
            if contains "$base" "${EXPECTED_FAIL[@]}"; then
                [ "$VERBOSE" = "1" ] && echo "  $base ... OK (expected fail)" >&2
            else
                COMPILE_FAIL+=("$base")
                echo -e "${RED}[COMPILE FAIL]${NC} $base"
                [ -n "$detail" ] && echo "  $detail"
            fi
            ;;
    esac
done

for base in "${FAIL_FILES[@]}"; do
    result_file="$RESULT_DIR/$base.result"
    if [ ! -f "$result_file" ]; then
        FAIL_BAD+=("$base (no result)")
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
            if [[ "$detail" == CRASHED* ]]; then
                echo -e "${RED}[COMPILER CRASH]${NC} $base ($detail)"
            else
                echo -e "${RED}[UNEXPECTED PASS]${NC} $base ($detail)"
            fi
            ;;
    esac
done

# Summary
echo ""
echo "========================"
echo "SUMMARY"
echo "========================"
echo "Total: $TOTAL files tested (with $JOBS parallel jobs)"
printf "Compile: ${GREEN}%d pass${NC} / ${RED}%d fail${NC}\n" "${#COMPILE_OK[@]}" "${#COMPILE_FAIL[@]}"
printf "Link:    ${GREEN}%d pass${NC} / ${RED}%d fail${NC}\n" "${#LINK_OK[@]}" "${#LINK_FAIL[@]}"
printf "Runtime: ${GREEN}%d pass${NC} / ${RED}%d crash${NC} / ${RED}%d mismatch${NC}\n" "$((${#LINK_OK[@]} - ${#RUNTIME_CRASH[@]} - ${#RETURN_MISMATCH[@]}))" "${#RUNTIME_CRASH[@]}" "${#RETURN_MISMATCH[@]}"
[ ${#FAIL_FILES[@]} -gt 0 ] && printf "_fail:   ${GREEN}%d correct${NC} / ${RED}%d wrong${NC}\n" "${#FAIL_OK[@]}" "${#FAIL_BAD[@]}"

# Show failures
if [ ${#COMPILE_FAIL[@]} -gt 0 ]; then
    echo -e "\n${RED}Compile failures (${#COMPILE_FAIL[@]}):${NC}"
    printf '  %s\n' "${COMPILE_FAIL[@]}" | head -20
    [ ${#COMPILE_FAIL[@]} -gt 20 ] && echo "  ... and $((${#COMPILE_FAIL[@]} - 20)) more"
fi

if [ ${#LINK_FAIL[@]} -gt 0 ]; then
    echo -e "\n${YELLOW}Link failures (${#LINK_FAIL[@]}) - likely need C++ features:${NC}"
    echo "  (vtables, constructors, exceptions, etc.)"
    printf '  %s\n' "${LINK_FAIL[@]}" | head -20
    [ ${#LINK_FAIL[@]} -gt 20 ] && echo "  ... and $((${#LINK_FAIL[@]} - 20)) more"
fi

if [ ${#RUNTIME_CRASH[@]} -gt 0 ]; then
    echo -e "\n${RED}Runtime crashes (${#RUNTIME_CRASH[@]}):${NC}"
    printf '  %s\n' "${RUNTIME_CRASH[@]}"
fi

if [ ${#RETURN_MISMATCH[@]} -gt 0 ]; then
    echo -e "\n${RED}Return value mismatches (${#RETURN_MISMATCH[@]}):${NC}"
    printf '  %s\n' "${RETURN_MISMATCH[@]}"
fi

[ ${#FAIL_BAD[@]} -gt 0 ] && {
    echo -e "\n${RED}_fail files that passed:${NC}"
    printf '  %s\n' "${FAIL_BAD[@]}"
}

echo ""
# NOTE: Return value mismatches now fail the build since __has_builtin has been fixed; runtime crashes also now fail the build
if [ ${#COMPILE_FAIL[@]} -eq 0 ] && [ ${#LINK_FAIL[@]} -eq 0 ] && [ ${#FAIL_BAD[@]} -eq 0 ] && [ ${#RETURN_MISMATCH[@]} -eq 0 ] && [ ${#RUNTIME_CRASH[@]} -eq 0 ]; then
    echo -e "${GREEN}RESULT: SUCCESS${NC}"
    exit 0
else
    echo -e "${RED}RESULT: FAILED${NC}"
    exit 1
fi
