#!/bin/bash
# Comprehensive Test Script for FlashCpp
# This script tests all .cpp files in the tests directory
# - Files with _fail suffix are expected to fail compilation
# - All other files are expected to pass compilation

# Navigate to the repository root (relative to this script's location)
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$REPO_ROOT"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Counters
PASS_COUNT=0
FAIL_COUNT=0
TOTAL_COUNT=0

# Arrays to store results
declare -a PASSED_TESTS
declare -a FAILED_TESTS

echo "=============================================="
echo "    FlashCpp Comprehensive Test Suite"
echo "=============================================="
echo ""

# Build the compiler if not built
if [ ! -f "x64/Debug/FlashCpp" ]; then
    echo "Building FlashCpp compiler..."
    make main CXX=clang++ > /dev/null 2>&1
    if [ $? -ne 0 ]; then
        echo -e "${RED}Failed to build FlashCpp compiler${NC}"
        exit 1
    fi
fi

# Auto-detect include paths
detect_include_paths() {
    local paths=""
    local output
    output=$(echo | clang++ -E -x c++ - -v 2>&1 | sed -n '/#include <...> search starts here:/,/End of search list/p' | grep '^ ')
    
    while IFS= read -r path; do
        path=$(echo "$path" | xargs)
        if [ -n "$path" ] && [ -d "$path" ]; then
            paths="$paths -I$path"
        fi
    done <<< "$output"
    
    echo "$paths"
}

INCLUDE_PATHS=$(detect_include_paths)

echo "Testing regular .cpp files (expected to pass)..."
echo "================================================"
echo ""

# Test regular files (expected to pass)
for test_file in tests/*.cpp; do
    # Skip if file doesn't exist
    [ -e "$test_file" ] || continue
    
    # Get filename without path
    filename=$(basename "$test_file")
    
    # Skip _fail files for now
    if [[ "$filename" == *_fail.cpp ]]; then
        continue
    fi
    
    # Skip directories and test suite files
    if [[ "$filename" == "FlashCppTest.cpp" ]] || [[ "$filename" == "FlashCppDebugTest.cpp" ]]; then
        continue
    fi
    
    TOTAL_COUNT=$((TOTAL_COUNT + 1))
    
    # Run FlashCpp compiler
    output=$(./x64/Debug/FlashCpp "$test_file" $INCLUDE_PATHS 2>&1)
    exit_code=$?
    
    # Check for success
    if echo "$output" | grep -q "Object file written successfully"; then
        echo -e "${GREEN}[PASS]${NC} $filename"
        PASS_COUNT=$((PASS_COUNT + 1))
        PASSED_TESTS+=("$filename")
        # Clean up generated object file
        rm -f "${filename%.cpp}.obj"
    else
        echo -e "${RED}[FAIL]${NC} $filename"
        FAIL_COUNT=$((FAIL_COUNT + 1))
        FAILED_TESTS+=("$filename - Expected to pass but failed")
        # Show first error line
        first_error=$(echo "$output" | grep -E "(Error|error)" | head -1)
        if [ -n "$first_error" ]; then
            echo "       Error: $first_error"
        fi
    fi
done

echo ""
echo "Testing _fail.cpp files (expected to fail)..."
echo "=============================================="
echo ""

# Test _fail files (expected to fail compilation)
for test_file in tests/*_fail.cpp; do
    # Skip if file doesn't exist
    [ -e "$test_file" ] || continue
    
    # Get filename without path
    filename=$(basename "$test_file")
    
    TOTAL_COUNT=$((TOTAL_COUNT + 1))
    
    # Run FlashCpp compiler - we expect this to fail
    output=$(./x64/Debug/FlashCpp "$test_file" $INCLUDE_PATHS 2>&1)
    exit_code=$?
    
    # Check that it failed (opposite of normal tests)
    if echo "$output" | grep -q "Object file written successfully"; then
        echo -e "${RED}[FAIL]${NC} $filename - UNEXPECTED SUCCESS (should fail)"
        FAIL_COUNT=$((FAIL_COUNT + 1))
        FAILED_TESTS+=("$filename - Expected to fail but succeeded")
        # Clean up generated object file
        rm -f "${filename%.cpp}.obj"
    else
        echo -e "${GREEN}[PASS]${NC} $filename - Failed as expected"
        PASS_COUNT=$((PASS_COUNT + 1))
        PASSED_TESTS+=("$filename")
    fi
done

echo ""
echo "=============================================="
echo "                 SUMMARY"
echo "=============================================="
echo ""
echo "Total tests: $TOTAL_COUNT"
echo -e "${GREEN}Passed: $PASS_COUNT${NC}"
echo -e "${RED}Failed: $FAIL_COUNT${NC}"
echo ""

if [ ${#FAILED_TESTS[@]} -gt 0 ]; then
    echo "Failed tests:"
    for test in "${FAILED_TESTS[@]}"; do
        echo "  - $test"
    done
    echo ""
fi

echo "=============================================="

# Exit with error if any tests failed
if [ $FAIL_COUNT -gt 0 ]; then
    exit 1
else
    exit 0
fi
