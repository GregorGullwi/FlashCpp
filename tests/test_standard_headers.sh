#!/bin/bash
# Standard Header Test Script for FlashCpp
# This script tests inclusion of various standard headers and generates a report

cd /home/runner/work/FlashCpp/FlashCpp

# Build the compiler if not built
if [ ! -f "x64/Debug/FlashCpp" ]; then
    make main CXX=clang++ 2>&1
fi

# Include paths for standard library (GCC 14)
INCLUDE_PATHS="-I/usr/include/c++/14 -I/usr/include/x86_64-linux-gnu/c++/14 -I/usr/include/c++/14/backward -I/usr/lib/llvm-18/lib/clang/18/include -I/usr/local/include -I/usr/include/x86_64-linux-gnu -I/usr/include"

# Test headers
HEADERS=(
    # C library wrappers
    "cstddef"
    "cstdint"
    "cstdlib"
    "cstring"
    "climits"
    "cstdio"
    "cmath"
    "cassert"
    "cerrno"
    "cfloat"
    
    # C++ utilities
    "utility"
    "type_traits"
    "limits"
    "initializer_list"
    "tuple"
    "any"
    "optional"
    "variant"
    
    # Containers
    "array"
    "vector"
    "deque"
    "list"
    "forward_list"
    "set"
    "map"
    "unordered_set"
    "unordered_map"
    "stack"
    "queue"
    
    # Strings
    "string"
    "string_view"
    
    # I/O
    "iostream"
    "istream"
    "ostream"
    "sstream"
    "fstream"
    
    # Memory
    "memory"
    "new"
    
    # Algorithms and functional
    "algorithm"
    "functional"
    "numeric"
    
    # Iterators
    "iterator"
    
    # Time
    "chrono"
    
    # Concepts (C++20)
    "concepts"
    
    # Ranges (C++20)
    "ranges"
    
    # Format (C++20)
    "format"
    
    # Coroutines (C++20)
    "coroutine"
    
    # Other C++20 headers
    "span"
    "bit"
    "compare"
    "source_location"
    "version"
    
    # Threading
    "thread"
    "mutex"
    "atomic"
    "condition_variable"
    "future"
    
    # Filesystem (C++17)
    "filesystem"
    
    # Regular expressions
    "regex"
    
    # Exception handling
    "exception"
    "stdexcept"
)

echo "=============================================="
echo "FlashCpp Standard Header Support Test Report"
echo "=============================================="
echo ""
echo "Date: $(date)"
echo "FlashCpp built: $(stat -c '%y' x64/Debug/FlashCpp 2>/dev/null || echo 'unknown')"
echo "GCC version: $(g++ --version | head -1)"
echo ""

# Results arrays
declare -A success_headers
declare -A failed_headers
declare -A error_messages

for header in "${HEADERS[@]}"; do
    # Create test file
    cat > test_header_temp.cpp << EOF
#include <$header>
int main() { return 0; }
EOF

    # Run FlashCpp and capture output
    output=$(./x64/Debug/FlashCpp test_header_temp.cpp $INCLUDE_PATHS 2>&1)
    exit_code=$?
    
    # Check for success - look for "Object file written successfully"
    if echo "$output" | grep -q "Object file written successfully"; then
        success_headers["$header"]=1
        echo "[PASS] <$header>"
    else
        failed_headers["$header"]=1
        # Extract first error message
        first_error=$(echo "$output" | grep -E "(Error|error|Internal compiler error|Invalid|terminate)" | head -1)
        error_messages["$header"]="$first_error"
        echo "[FAIL] <$header>: $first_error"
    fi
done

# Clean up temp files
rm -f test_header_temp.cpp test_header_temp.obj

echo ""
echo "=============================================="
echo "                   SUMMARY"
echo "=============================================="
echo ""

# Count results
pass_count=${#success_headers[@]}
fail_count=${#failed_headers[@]}
total=$((pass_count + fail_count))

echo "Total headers tested: $total"
echo "Passed: $pass_count"
echo "Failed: $fail_count"
echo ""

if [ $pass_count -gt 0 ]; then
    echo "=== Successfully Included Headers ==="
    for header in "${!success_headers[@]}"; do
        echo "  - <$header>"
    done | sort
    echo ""
fi

if [ $fail_count -gt 0 ]; then
    echo "=== Failed Headers ==="
    for header in "${!failed_headers[@]}"; do
        echo "  - <$header>"
        echo "    Error: ${error_messages[$header]}"
    done | sort
    echo ""
fi

echo "=============================================="
echo "         ISSUES TO FIX FOR STD HEADERS"
echo "=============================================="
echo ""
echo "Based on the test results, the following issues need to be addressed:"
echo ""

# Analyze common error patterns
echo "1. PREPROCESSOR ISSUES:"
echo "   - Feature test macros (__cpp_*) need to be defined"
echo "   - __has_feature/__has_builtin/__has_cpp_attribute intrinsics not handled"
echo "   - __SANITIZE_THREAD__ and similar sanitizer macros"
echo ""

echo "2. PARSER/LEXER ISSUES:"
echo "   - Complex preprocessor expressions with << operator in conditions"
echo "   - Some C++20 syntax may not be fully supported"
echo ""

echo "3. BUILTIN FUNCTIONS:"
echo "   - __builtin_* functions need to be recognized"
echo "   - Compiler intrinsics used by standard library"
echo ""

echo "=============================================="
echo "              END OF REPORT"
echo "=============================================="
