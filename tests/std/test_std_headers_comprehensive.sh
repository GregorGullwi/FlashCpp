#!/bin/bash
# Comprehensive Standard Header Test Script
# Tests each standard header individually with timeout protection

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
cd "$REPO_ROOT"

# Build if needed
if [ ! -f "x64/Debug/FlashCpp" ]; then
    echo "Building FlashCpp..."
    make main CXX=clang++ > /dev/null 2>&1 || { echo "Build failed"; exit 1; }
fi

# Auto-detect include paths
INCLUDE_PATHS="-I/usr/lib/gcc/x86_64-linux-gnu/14/../../../../include/c++/14 -I/usr/lib/gcc/x86_64-linux-gnu/14/../../../../include/x86_64-linux-gnu/c++/14 -I/usr/lib/llvm-18/lib/clang/18/include"

# Test files
TEST_FILES=(
    "test_std_type_traits.cpp"
    "test_std_string_view.cpp"
    "test_std_string.cpp"
    "test_std_iostream.cpp"
    "test_std_tuple.cpp"
    "test_std_vector.cpp"
    "test_std_array.cpp"
    "test_std_algorithm.cpp"
    "test_std_utility.cpp"
    "test_std_memory.cpp"
    "test_std_functional.cpp"
    "test_std_map.cpp"
    "test_std_set.cpp"
    "test_std_optional.cpp"
    "test_std_variant.cpp"
    "test_std_any.cpp"
    "test_std_span.cpp"
    "test_std_concepts.cpp"
    "test_std_ranges.cpp"
    "test_std_limits.cpp"
    "test_std_chrono.cpp"
)

echo "=========================================="
echo "FlashCpp Standard Headers Test Report"
echo "=========================================="
echo ""

declare -a COMPILED=()
declare -a TIMEOUT=()
declare -a FAILED=()

for test_file in "${TEST_FILES[@]}"; do
    test_path="tests/std/$test_file"
    obj_file="${test_file%.cpp}.obj"
    
    echo -n "Testing $test_file... "
    
    # Clean up previous object file
    rm -f "$obj_file"
    
    # Run with 10 second timeout
    timeout 10 ./x64/Debug/FlashCpp "$test_path" $INCLUDE_PATHS > /tmp/flashcpp_test.log 2>&1
    exit_code=$?
    
    if [ $exit_code -eq 124 ]; then
        echo "TIMEOUT"
        TIMEOUT+=("$test_file")
    elif [ -f "$obj_file" ]; then
        echo "COMPILED"
        COMPILED+=("$test_file")
        rm -f "$obj_file"
    else
        echo "FAILED"
        FAILED+=("$test_file")
        # Get first error
        first_error=$(grep -E "(Error|error)" /tmp/flashcpp_test.log | head -1 | cut -c1-100)
        echo "    Error: $first_error"
    fi
done

echo ""
echo "=========================================="
echo "                 SUMMARY"
echo "=========================================="
echo ""
echo "Total: ${#TEST_FILES[@]} test files"
echo "Compiled: ${#COMPILED[@]}"
echo "Timeout: ${#TIMEOUT[@]}"
echo "Failed: ${#FAILED[@]}"
echo ""

if [ ${#COMPILED[@]} -gt 0 ]; then
    echo "=== Successfully Compiled ==="
    printf '  %s\n' "${COMPILED[@]}"
    echo ""
fi

if [ ${#TIMEOUT[@]} -gt 0 ]; then
    echo "=== Timed Out (>10s) ==="
    printf '  %s\n' "${TIMEOUT[@]}"
    echo ""
fi

if [ ${#FAILED[@]} -gt 0 ]; then
    echo "=== Failed to Compile ==="
    printf '  %s\n' "${FAILED[@]}"
    echo ""
fi

echo "=========================================="
