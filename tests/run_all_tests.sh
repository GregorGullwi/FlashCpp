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
    "test_cstddef.cpp"
    "test_cstdio_puts.cpp"
    "test_cstdlib.cpp"
)

# Expected link failures - files that compile but require external C helper files
EXPECTED_LINK_FAIL=(
    "test_external_abi.cpp"
    "test_external_abi_simple.cpp"
    "test_varargs.cpp"
    "test_stack_overflow.cpp"
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
    
    rm -f "$obj" "$exe"
    
    # Compile
    compile_output=$(./x64/Debug/FlashCpp "$f" 2>&1)
    if [ -f "$obj" ]; then
        COMPILE_OK+=("$base")
        
        # Link silently
        if gcc -o "$exe" "$obj" 2>/dev/null; then
            LINK_OK+=("$base")
            rm -f "$exe"
        else
            # Check if this is an expected link failure
            if contains "$base" "${EXPECTED_LINK_FAIL[@]}"; then
                # Expected link failure - don't print
                LINK_OK+=("$base")  # Count as OK since compile succeeded
            else
                # Only print unexpected link failures
                echo -e "${RED}[LINK FAIL]${NC} $base"
                LINK_FAIL+=("$base")
            fi
        fi
    else
        if contains "$base" "${EXPECTED_FAIL[@]}"; then
            # Expected failure - don't print
            :
        else
            # Print unexpected compile failures
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
    N=0
    for base in "${FAIL_FILES[@]}"; do
        ((N++))
        f="tests/$base"
        obj="${base%.cpp}.obj"
        
        rm -f "$obj"
        
        if ./x64/Debug/FlashCpp "$f" > /dev/null 2>&1 && [ -f "$obj" ]; then
            echo -e "${RED}[UNEXPECTED PASS]${NC} $base (should have failed)"
            FAIL_BAD+=("$base")
            rm -f "$obj"
        else
            FAIL_OK+=("$base")
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
