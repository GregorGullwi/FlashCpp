#!/bin/bash
# FlashCpp vs Clang vs GCC Compilation Speed Benchmark
# Measures end-to-end compile time for the C++20 integration test

set -e

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
REPO_ROOT="$SCRIPT_DIR/../.."
TEST_FILE="$SCRIPT_DIR/cpp20_simple_integration_test.cpp"
FLASHCPP="$REPO_ROOT/x64/Debug/FlashCpp"
ITERATIONS=${1:-10}

echo "======================================================================"
echo "FlashCpp Compilation Speed Benchmark"
echo "======================================================================"
echo ""
echo "Test file: cpp20_simple_integration_test.cpp (~860 lines, 490 test points)"
echo "Iterations: $ITERATIONS per compiler"
echo "Mode: compile-only (-c), no optimization"
echo ""

# Check prerequisites
if [ ! -f "$FLASHCPP" ]; then
    echo "ERROR: FlashCpp binary not found at $FLASHCPP"
    echo "Build it first: make main CXX=clang++"
    exit 1
fi

if ! command -v clang++ &> /dev/null; then
    echo "ERROR: clang++ not found"
    exit 1
fi

if ! command -v g++ &> /dev/null; then
    echo "ERROR: g++ not found"
    exit 1
fi

echo "Compiler versions:"
echo "  Clang: $(clang++ --version | head -1)"
echo "  GCC:   $(g++ --version | head -1)"
echo "  FlashCpp: $($FLASHCPP --version 2>&1 | head -1 || echo 'dev build')"
echo ""

benchmark() {
    local name="$1"
    shift
    local cmd=("$@")
    local times=()

    for i in $(seq 1 $ITERATIONS); do
        start=$(date +%s%N)
        "${cmd[@]}" > /dev/null 2>&1 || true
        end=$(date +%s%N)
        elapsed=$(( (end - start) / 1000000 ))
        times+=($elapsed)
    done

    local total=0
    local min=${times[0]}
    local max=${times[0]}
    for t in "${times[@]}"; do
        total=$((total + t))
        [ $t -lt $min ] && min=$t
        [ $t -gt $max ] && max=$t
    done
    local avg=$((total / ITERATIONS))

    printf "  %-20s avg=%3d ms  min=%3d ms  max=%3d ms\n" "$name" "$avg" "$min" "$max"
    eval "${name//[^a-zA-Z0-9_]/_}_AVG=$avg"
    eval "${name//[^a-zA-Z0-9_]/_}_MIN=$min"
    eval "${name//[^a-zA-Z0-9_]/_}_MAX=$max"
}

echo "--- End-to-end compilation (source -> object file) ---"
echo ""
benchmark "Clang" clang++ -std=c++20 "$TEST_FILE" -c -o /tmp/bench_clang.o
benchmark "GCC" g++ -std=c++20 "$TEST_FILE" -c -o /tmp/bench_gcc.o
benchmark "FlashCpp" "$FLASHCPP" "$TEST_FILE" -o /tmp/bench_flash.o
echo ""

echo "--- Parse-only (frontend only, no codegen) ---"
echo ""
benchmark "Clang_syntax" clang++ -std=c++20 -fsyntax-only "$TEST_FILE"
benchmark "GCC_syntax" g++ -std=c++20 -fsyntax-only "$TEST_FILE"
echo "  (FlashCpp does not have a parse-only mode)"
echo ""

echo "======================================================================"
echo "SUMMARY"
echo "======================================================================"
echo ""
echo "| Compiler        | Avg (ms) | Min (ms) | Max (ms) |"
echo "|-----------------|----------|----------|----------|"
printf "| Clang++ 18.1.3  | %8d | %8d | %8d |\n" "$Clang_AVG" "$Clang_MIN" "$Clang_MAX"
printf "| GCC 13.3.0      | %8d | %8d | %8d |\n" "$GCC_AVG" "$GCC_MIN" "$GCC_MAX"
printf "| FlashCpp (debug)| %8d | %8d | %8d |\n" "$FlashCpp_AVG" "$FlashCpp_MIN" "$FlashCpp_MAX"
echo ""
echo "Note: FlashCpp is compiled in debug mode (-g). Release builds are faster."
echo "Note: FlashCpp performs full compilation (parse + codegen + ELF output)"
echo "      in a single pass, while Clang/GCC use multi-stage pipelines."
echo ""
echo "FlashCpp internal timing breakdown (last run):"
"$FLASHCPP" "$TEST_FILE" -o /tmp/bench_flash.o 2>&1 | grep -E "^(Phase|Preproc|Lexer|Pars|IR Conv|Deferred|Code Gen|Other|TOTAL|---)" || true

# Cleanup
rm -f /tmp/bench_clang.o /tmp/bench_gcc.o /tmp/bench_flash.o
