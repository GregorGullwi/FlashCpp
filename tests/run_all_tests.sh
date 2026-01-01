#!/bin/bash
# FlashCpp ELF Test Runner - Port of test_reference_files.ps1 for Linux
# Tests compilation and linking of all test files

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$REPO_ROOT"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

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
EXPECTED_LINK_FAIL=(
    "test_external_abi.cpp"               # Needs external C helper functions from test_external_abi_helper.c
    "test_external_abi_simple.cpp"        # Needs external C helper functions from test_external_abi_helper.c
)

# Results
declare -a COMPILE_OK=()
declare -a COMPILE_FAIL=()
declare -a LINK_OK=()
declare -a LINK_FAIL=()
declare -a FAIL_OK=()
declare -a FAIL_BAD=()

contains() {
    local e match="$1"
    shift
    for e; do [[ "$e" == "$match" ]] && return 0; done
    return 1
}

# Get test files with main()
TEST_FILES=()
FAIL_FILES=()
for f in tests/*.cpp; do
    [ -f "$f" ] || continue
    base=$(basename "$f")
    if grep -q '\bint\s\+main\s*(' "$f" || grep -q '\bvoid\s\+main\s*(' "$f"; then
        [[ "$base" == *"_fail.cpp" ]] && FAIL_FILES+=("$base") || TEST_FILES+=("$base")
    fi
done

TOTAL=${#TEST_FILES[@]}
TOTAL_FAIL=${#FAIL_FILES[@]}
echo "Testing $TOTAL files..."
echo ""

# Test regular files
N=0
for base in "${TEST_FILES[@]}"; do
    ((N++))
    f="tests/$base"
    obj="${base%.cpp}.obj"
    exe="/tmp/${base%.cpp}_exe"
    
    # Print current file being tested (for debugging hangs)
    echo -n "[$N/$TOTAL] Testing $base... " >&2
    
    rm -f "$obj" "$exe"
    
    # Clean up any old crash logs for this test
    rm -f flashcpp_crash_*.log
    
	# Compile (with 30 second timeout to avoid hangs)
	extra_flags=()
	if [ "$base" == "test_no_access_control_flag.cpp" ]; then
		extra_flags+=("-fno-access-control")
	fi
	compile_output=$(timeout 30 ./x64/Debug/FlashCpp "${extra_flags[@]}" "$f" 2>&1)
    compile_exit=$?
    
    # Check if compiler crashed (exit code 134 = SIGABRT, 136 = SIGFPE, 139 = SIGSEGV)
    if [ $compile_exit -eq 134 ] || [ $compile_exit -eq 136 ] || [ $compile_exit -eq 139 ]; then
        echo "" >&2
        echo -e "${RED}[COMPILER CRASH]${NC} $base (signal: $compile_exit)"
        # Find and display the most recent crash log
        latest_crash=$(ls -t flashcpp_crash_*.log 2>/dev/null | head -1)
        if [ -n "$latest_crash" ]; then
            echo "=== Crash Stack Trace ===" >&2
            grep -A 50 "=== Stack Trace ===" "$latest_crash" | head -20 >&2
            rm -f "$latest_crash"
        fi
        COMPILE_FAIL+=("$base (CRASHED)")
        rm -f "$obj"
        continue
    fi
    
    if [ -f "$obj" ]; then
        COMPILE_OK+=("$base")
        
        # Choose linker based on file type
        link_output=$(clang++ -no-pie -o "$exe" "$obj" -lstdc++ -lc 2>&1)
        link_exit_code=$?
        
        if [ $link_exit_code -eq 0 ]; then
            LINK_OK+=("$base")
            rm -f "$exe"
            echo "OK" >&2
        else
            # Check if this is an expected link failure
            if contains "$base" "${EXPECTED_LINK_FAIL[@]}"; then
                # Expected link failure - don't print
                LINK_OK+=("$base")  # Count as OK since compile succeeded
                echo "OK (expected link fail)" >&2
            else
                # Only print unexpected link failures
                echo "" >&2
                echo -e "${RED}[LINK FAIL]${NC} $base"
                # Show link errors (undefined references and linker errors)
                link_errors=$(echo "$link_output" | grep -E "undefined reference to|error: linker command failed" | head -5)
                if [ -n "$link_errors" ]; then
                    echo "$link_errors" | sed 's/^/  /' >&2
                fi
                LINK_FAIL+=("$base")
            fi
        fi
    else
        if contains "$base" "${EXPECTED_FAIL[@]}"; then
            # Expected failure - don't print
            echo "OK (expected fail)" >&2
        else
            # Print unexpected compile failures
            echo "" >&2
            echo -e "${RED}[COMPILE FAIL]${NC} $base"
            COMPILE_FAIL+=("$base")
            # Show first error line
            first_error=$(echo "$compile_output" | grep -i "error" | head -1)
            [ -n "$first_error" ] && echo "  $first_error"
        fi
    fi
    rm -f "$obj"
done

# Test _fail files
if [ ${#FAIL_FILES[@]} -gt 0 ]; then
    echo "" >&2
    echo "Testing _fail files (expected to fail compilation)..." >&2
    N=0
    for base in "${FAIL_FILES[@]}"; do
        ((N++))
        f="tests/$base"
        obj="${base%.cpp}.obj"
        
        echo -n "[$N/$TOTAL_FAIL] Testing $base... " >&2
        
        rm -f "$obj"
        
        # Clean up any old crash logs for this test
        rm -f flashcpp_crash_*.log
        
        compile_output=$(timeout 30 ./x64/Debug/FlashCpp "$f" 2>&1)
        compile_exit=$?
        
        # Check if compiler crashed
        if [ $compile_exit -eq 134 ] || [ $compile_exit -eq 136 ] || [ $compile_exit -eq 139 ]; then
            echo "" >&2
            echo -e "${RED}[COMPILER CRASH]${NC} $base (should fail cleanly, not crash!)"
            latest_crash=$(ls -t flashcpp_crash_*.log 2>/dev/null | head -1)
            if [ -n "$latest_crash" ]; then
                echo "=== Crash Stack Trace ===" >&2
                grep -A 50 "=== Stack Trace ===" "$latest_crash" | head -20 >&2
                rm -f "$latest_crash"
            fi
            FAIL_BAD+=("$base (CRASHED)")
            rm -f "$obj"
            continue
        fi
        
        if [ -f "$obj" ]; then
            echo "" >&2
            echo -e "${RED}[UNEXPECTED PASS]${NC} $base (should have failed)"
            FAIL_BAD+=("$base")
            rm -f "$obj"
        else
            FAIL_OK+=("$base")
            echo "OK (failed as expected)" >&2
        fi
    done
fi

# Summary
echo ""
echo "========================"
echo "SUMMARY"
echo "========================"
echo "Total: $TOTAL files tested"
printf "Compile: ${GREEN}%d pass${NC} / ${RED}%d fail${NC}\n" "${#COMPILE_OK[@]}" "${#COMPILE_FAIL[@]}"
printf "Link:    ${GREEN}%d pass${NC} / ${RED}%d fail${NC}\n" "${#LINK_OK[@]}" "${#LINK_FAIL[@]}"
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
fi

[ ${#FAIL_BAD[@]} -gt 0 ] && {
    echo -e "\n${RED}_fail files that passed:${NC}"
    printf '  %s\n' "${FAIL_BAD[@]}"
}

echo ""
if [ ${#COMPILE_FAIL[@]} -eq 0 ] && [ ${#LINK_FAIL[@]} -eq 0 ] && [ ${#FAIL_BAD[@]} -eq 0 ]; then
    echo -e "${GREEN}RESULT: SUCCESS${NC}"
    exit 0
else
    echo -e "${RED}RESULT: FAILED${NC}"
    exit 1
fi
