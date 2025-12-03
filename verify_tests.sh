#!/bin/bash

echo "Verifying test files with clang++ in C++20 mode"
echo "================================================"
echo ""

# Test files to verify based on expected failures list
test_files=(
    "tests/test_type_traits_intrinsics.cpp"
    "tests/test_constexpr_structs.cpp"
    "tests/test_cstddef.cpp"
    "tests/test_cstdio_puts.cpp"
    "tests/test_ctad_struct_lifecycle.cpp"
    "tests/test_va_implementation.cpp"
    "tests/test_lambda_cpp20_comprehensive.cpp"
)

pass_count=0
fail_count=0

for file in "${test_files[@]}"; do
    if [ ! -f "$file" ]; then
        echo "SKIP: $file (not found)"
        continue
    fi
    
    echo "Testing: $file"
    
    # Try to compile with clang++ -std=c++20 -fsyntax-only
    if clang++ -std=c++20 -fsyntax-only "$file" 2>&1 | tee /tmp/clang_output.txt | grep -q "error:"; then
        echo "  ❌ FAILED - Not valid C++20"
        echo "  First error:"
        grep "error:" /tmp/clang_output.txt | head -1 | sed 's/^/    /'
        ((fail_count++))
    else
        echo "  ✅ PASSED - Valid C++20"
        ((pass_count++))
    fi
    echo ""
done

echo "================================================"
echo "Summary: $pass_count passed, $fail_count failed"
echo "================================================"
