#!/bin/bash
# FlashCpp Return Value Validator
# Compiles and runs test files to check if return values are in the 0-255 range
# Documents findings about out-of-range return values

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$REPO_ROOT"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

echo "FlashCpp Return Value Validator"
echo "================================"
echo ""

# Build compiler if needed
if [ ! -f "x64/Debug/FlashCpp" ]; then
    echo "Building FlashCpp compiler..."
    make main CXX=clang++ > /dev/null 2>&1 || { echo -e "${RED}Build failed${NC}"; exit 1; }
    echo -e "${GREEN}Build successful${NC}"
fi

# Expected failures (files that should not compile)
EXPECTED_FAIL=(
    "test_cstdio_puts.cpp"
)

# Expected link failures
EXPECTED_LINK_FAIL=(
    "test_external_abi.cpp"
    "test_external_abi_simple.cpp"
    "test_dynamic_cast_debug.cpp"
    "test_virtual_inheritance.cpp"
    "test_varargs.cpp"
    "test_cstdlib.cpp"
)

# Known test files with valid out-of-range return values
# These tests intentionally return values > 255 to test arithmetic/features
KNOWN_OUT_OF_RANGE=(
    "test_conditional_sum.cpp"
    "test_designated_init.cpp"
    "test_enum.cpp"
)

# Results arrays
declare -A VALID_RETURNS=()
declare -A OUT_OF_RANGE_VALID=()
declare -A OUT_OF_RANGE_UNKNOWN=()
declare -A COMPILE_FAIL=()
declare -A LINK_FAIL=()
declare -A RUNTIME_CRASH=()

contains() {
    local e match="$1"
    shift
    for e; do [[ "$e" == "$match" ]] && return 0; done
    return 1
}

# Get test files with main()
TEST_FILES=()
for f in tests/*.cpp; do
    [ -f "$f" ] || continue
    base=$(basename "$f")
    
    # Skip _fail files
    [[ "$base" == *"_fail.cpp" ]] && continue
    
    # Check if file has main function
    if grep -q '\bint[[:space:]]\+main[[:space:]]*(' "$f" || grep -q '\bvoid[[:space:]]\+main[[:space:]]*(' "$f"; then
        TEST_FILES+=("$base")
    fi
done

TOTAL=${#TEST_FILES[@]}
echo "Found $TOTAL test files with main()"
echo ""

# Process each test file
N=0
for base in "${TEST_FILES[@]}"; do
    ((N++))
    f="tests/$base"
    obj="${base%.cpp}.obj"
    exe="/tmp/${base%.cpp}_exe"
    
    printf "[%3d/%3d] Testing %-50s " "$N" "$TOTAL" "$base"
    
    rm -f "$obj" "$exe"
    
    # Compile
    compile_output=$(timeout 30 ./x64/Debug/FlashCpp "$f" 2>&1)
    compile_exit=$?
    
    # Check for compiler crash
    if [ $compile_exit -eq 134 ] || [ $compile_exit -eq 136 ] || [ $compile_exit -eq 139 ]; then
        echo -e "${RED}COMPILER CRASH${NC}"
        COMPILE_FAIL["$base"]="Compiler crashed (signal: $compile_exit)"
        rm -f "$obj"
        continue
    fi
    
    # Check if compilation succeeded
    if [ ! -f "$obj" ]; then
        if contains "$base" "${EXPECTED_FAIL[@]}"; then
            echo -e "${YELLOW}EXPECTED FAIL${NC}"
        else
            echo -e "${RED}COMPILE FAIL${NC}"
            COMPILE_FAIL["$base"]="Compilation failed"
        fi
        rm -f "$obj"
        continue
    fi
    
    # Link
    LINKER="${CXX:-clang++}"
    link_output=$($LINKER -no-pie -o "$exe" "$obj" -lstdc++ -lc 2>&1)
    link_exit=$?
    
    if [ $link_exit -ne 0 ]; then
        if contains "$base" "${EXPECTED_LINK_FAIL[@]}"; then
            echo -e "${YELLOW}EXPECTED LINK FAIL${NC}"
        else
            echo -e "${RED}LINK FAIL${NC}"
            LINK_FAIL["$base"]="Linking failed"
        fi
        rm -f "$obj" "$exe"
        continue
    fi
    
    # Run the executable and get return value
    # Capture stderr to detect actual crashes vs normal returns
    stderr_output=$(timeout 5 "$exe" 2>&1 > /dev/null)
    return_value=$?
    
    # Check for timeout (exit code 124)
    if [ $return_value -eq 124 ]; then
        echo -e "${RED}TIMEOUT${NC}"
        RUNTIME_CRASH["$base"]="Execution timeout (infinite loop or hang)"
        rm -f "$obj" "$exe"
        continue
    fi
    
    # Check for actual crashes by looking for crash indicators in stderr
    # Real crashes will have messages like "Segmentation fault", "Illegal instruction", "Aborted", etc.
    # Also check for "dumped core" which timeout uses
    if echo "$stderr_output" | grep -qiE "(segmentation fault|illegal instruction|aborted|bus error|floating point exception|killed|dumped core)"; then
        # This is a real crash
        signal=$((return_value - 128))
        echo -e "${RED}CRASH (signal $signal)${NC}"
        RUNTIME_CRASH["$base"]="Runtime crash (signal: $signal)"
        rm -f "$obj" "$exe"
        continue
    fi
    
    # Check if return value is in valid range (0-255)
    if [ $return_value -ge 0 ] && [ $return_value -le 255 ]; then
        echo -e "${GREEN}OK${NC} (returned $return_value)"
        VALID_RETURNS["$base"]=$return_value
    else
        # This shouldn't happen on Linux (return values are masked to 0-255)
        # but we'll check anyway
        echo -e "${YELLOW}OUT OF RANGE${NC} (returned $return_value)"
        
        if contains "$base" "${KNOWN_OUT_OF_RANGE[@]}"; then
            OUT_OF_RANGE_VALID["$base"]=$return_value
        else
            OUT_OF_RANGE_UNKNOWN["$base"]=$return_value
        fi
    fi
    
    rm -f "$obj" "$exe"
done

# Generate summary
echo ""
echo "================================"
echo "SUMMARY"
echo "================================"
echo "Total files tested: $TOTAL"
echo -e "Valid returns (0-255): ${GREEN}${#VALID_RETURNS[@]}${NC}"
echo -e "Out of range (valid): ${YELLOW}${#OUT_OF_RANGE_VALID[@]}${NC}"
echo -e "Out of range (unknown): ${YELLOW}${#OUT_OF_RANGE_UNKNOWN[@]}${NC}"
echo -e "Compile failures: ${RED}${#COMPILE_FAIL[@]}${NC}"
echo -e "Link failures: ${RED}${#LINK_FAIL[@]}${NC}"
echo -e "Runtime crashes: ${RED}${#RUNTIME_CRASH[@]}${NC}"
echo ""

# Show details
if [ ${#OUT_OF_RANGE_UNKNOWN[@]} -gt 0 ]; then
    echo -e "${YELLOW}Files with unknown out-of-range return values:${NC}"
    for file in "${!OUT_OF_RANGE_UNKNOWN[@]}"; do
        echo "  $file: ${OUT_OF_RANGE_UNKNOWN[$file]}"
    done
    echo ""
fi

if [ ${#RUNTIME_CRASH[@]} -gt 0 ]; then
    echo -e "${RED}Runtime crashes:${NC}"
    for file in "${!RUNTIME_CRASH[@]}"; do
        echo "  $file: ${RUNTIME_CRASH[$file]}"
    done
    echo ""
fi

if [ ${#COMPILE_FAIL[@]} -gt 0 ]; then
    echo -e "${RED}Compilation failures:${NC}"
    count=0
    for file in "${!COMPILE_FAIL[@]}"; do
        if ! contains "$file" "${EXPECTED_FAIL[@]}"; then
            echo "  $file: ${COMPILE_FAIL[$file]}"
            ((count++))
        fi
    done
    if [ $count -eq 0 ]; then
        echo "  (all expected)"
    fi
    echo ""
fi

if [ ${#LINK_FAIL[@]} -gt 0 ]; then
    echo -e "${RED}Link failures:${NC}"
    count=0
    for file in "${!LINK_FAIL[@]}"; do
        if ! contains "$file" "${EXPECTED_LINK_FAIL[@]}"; then
            echo "  $file: ${LINK_FAIL[$file]}"
            ((count++))
        fi
    done
    if [ $count -eq 0 ]; then
        echo "  (all expected)"
    fi
    echo ""
fi

# Note about return values
echo "================================"
echo "NOTE ABOUT RETURN VALUES"
echo "================================"
echo "On Unix/Linux systems, the return value from main() is masked to 0-255."
echo "If a program returns a value like 300, the shell will only see 300 & 0xFF = 44."
echo "This means:"
echo "  - Return values > 255 are automatically truncated"
echo "  - The actual value returned may differ from the intended value"
echo "  - This is expected behavior and not a crash"
echo ""

echo -e "${GREEN}VALIDATION COMPLETE${NC}"
