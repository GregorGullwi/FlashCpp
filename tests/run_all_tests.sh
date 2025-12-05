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
EXPECTED_FAIL=("test_cstddef.cpp" "test_cstdio_puts.cpp")

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
echo "Files: $TOTAL tests, $TOTAL_FAIL _fail files"
echo ""

# Test regular files
N=0
for base in "${TEST_FILES[@]}"; do
    ((N++))
    f="tests/$base"
    obj="${base%.cpp}.obj"
    exe="/tmp/${base%.cpp}_exe"
    
    rm -f "$obj" "$exe"
    printf "[%d/%d] %-40s " "$N" "$TOTAL" "$base"
    
    # Compile
    if ./x64/Debug/FlashCpp "$f" > /dev/null 2>&1 && [ -f "$obj" ]; then
        printf "${GREEN}OK${NC} "
        COMPILE_OK+=("$base")
        
        # Link
        if gcc -o "$exe" "$obj" 2>/dev/null; then
            printf "${GREEN}LINK${NC}\n"
            LINK_OK+=("$base")
            rm -f "$exe"
        else
            printf "${RED}LINK FAIL${NC}\n"
            LINK_FAIL+=("$base")
        fi
    else
        if contains "$base" "${EXPECTED_FAIL[@]}"; then
            printf "${YELLOW}FAIL (exp)${NC}\n"
        else
            printf "${RED}FAIL${NC}\n"
            COMPILE_FAIL+=("$base")
        fi
    fi
    rm -f "$obj"
done

# Test _fail files
if [ ${#FAIL_FILES[@]} -gt 0 ]; then
    echo ""
    N=0
    for base in "${FAIL_FILES[@]}"; do
        ((N++))
        f="tests/$base"
        obj="${base%.cpp}.obj"
        
        rm -f "$obj"
        printf "[%d/%d] %-40s " "$N" "$TOTAL_FAIL" "$base"
        
        if ./x64/Debug/FlashCpp "$f" > /dev/null 2>&1 && [ -f "$obj" ]; then
            printf "${RED}PASS (bad!)${NC}\n"
            FAIL_BAD+=("$base")
            rm -f "$obj"
        else
            printf "${GREEN}FAIL (good)${NC}\n"
            FAIL_OK+=("$base")
        fi
    done
fi

# Summary
echo ""
echo "========================"
echo "SUMMARY"
echo "========================"
printf "Compile: ${GREEN}%d${NC} / ${RED}%d${NC}\n" "${#COMPILE_OK[@]}" "${#COMPILE_FAIL[@]}"
printf "Link:    ${GREEN}%d${NC} / ${RED}%d${NC}\n" "${#LINK_OK[@]}" "${#LINK_FAIL[@]}"
[ ${#FAIL_FILES[@]} -gt 0 ] && printf "_fail:   ${GREEN}%d${NC} / ${RED}%d${NC}\n" "${#FAIL_OK[@]}" "${#FAIL_BAD[@]}"

# Show failures
[ ${#COMPILE_FAIL[@]} -gt 0 ] && {
    echo -e "\n${RED}Compile failures:${NC}"
    printf '  %s\n' "${COMPILE_FAIL[@]}"
}

[ ${#LINK_FAIL[@]} -gt 0 ] && {
    echo -e "\n${RED}Link failures:${NC}"
    printf '  %s\n' "${LINK_FAIL[@]}"
}

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
