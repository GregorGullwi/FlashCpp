#!/bin/bash

echo "Testing template and modern C++ features..."
echo "============================================"

tests=(
    "tests/test_constexpr_comprehensive.cpp"
    "tests/test_lambda_captures_comprehensive.cpp"
    "tests/test_constexpr_structs.cpp"
    "tests/test_type_traits_intrinsics_working.cpp"
    "tests/test_ctad_struct_lifecycle.cpp"
)

for test in "${tests[@]}"; do
    echo ""
    echo "Testing: $test"
    base=$(basename "$test" .cpp)
    ./x64/Debug/FlashCpp "$test" 2>&1 | tail -5 | head -3
    if [ -f "${base}.obj" ]; then
        echo "  ✅ Compiled successfully"
    else
        echo "  ❌ Failed to compile"
    fi
done
