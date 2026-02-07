#!/bin/bash
# FlashCpp vs Clang vs GCC Compilation Speed Benchmark
# Measures end-to-end compile time for the C++20 integration test

set -e

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
REPO_ROOT="$SCRIPT_DIR/../.."
TEST_FILE="$SCRIPT_DIR/cpp20_simple_integration_test.cpp"
FLASHCPP_DEBUG="$REPO_ROOT/x64/Debug/FlashCpp"
FLASHCPP_RELEASE="$REPO_ROOT/x64/Release/FlashCpp"
ITERATIONS=${1:-20}

echo "======================================================================"
echo "FlashCpp Compilation Speed Benchmark"
echo "======================================================================"
echo ""
echo "Test file: cpp20_simple_integration_test.cpp (~860 lines, 490 test points)"
echo "Iterations: $ITERATIONS per compiler"
echo ""

# Check prerequisites
HAS_DEBUG=0
HAS_RELEASE=0
[ -f "$FLASHCPP_DEBUG" ] && HAS_DEBUG=1
[ -f "$FLASHCPP_RELEASE" ] && HAS_RELEASE=1

if [ $HAS_DEBUG -eq 0 ] && [ $HAS_RELEASE -eq 0 ]; then
    echo "ERROR: No FlashCpp binary found."
    echo "Build with: make main CXX=clang++        (debug)"
    echo "       or:  make release CXX=clang++      (release)"
    exit 1
fi

if ! command -v clang++ &> /dev/null; then
    echo "WARNING: clang++ not found, skipping"
fi

if ! command -v g++ &> /dev/null; then
    echo "WARNING: g++ not found, skipping"
fi

echo "Compiler versions:"
command -v clang++ &> /dev/null && echo "  Clang: $(clang++ --version | head -1)"
command -v g++ &> /dev/null && echo "  GCC:   $(g++ --version | head -1)"
[ $HAS_DEBUG -eq 1 ] && echo "  FlashCpp debug:   $FLASHCPP_DEBUG"
[ $HAS_RELEASE -eq 1 ] && echo "  FlashCpp release: $FLASHCPP_RELEASE"
echo ""

benchmark() {
    local name="$1"
    shift
    local cmd=("$@")
    local times=()
    local failures=0

    for i in $(seq 1 $ITERATIONS); do
        start=$(date +%s%N)
        if "${cmd[@]}" > /dev/null 2>&1; then
            end=$(date +%s%N)
            elapsed=$(( (end - start) / 1000000 ))
            times+=($elapsed)
        else
            end=$(date +%s%N)
            failures=$((failures + 1))
        fi
    done

    if [ ${#times[@]} -eq 0 ]; then
        printf "| %-28s | %6s | %6s | %6s | FAILED (all %d iterations)\n" "$name" "---" "---" "---" "$failures"
        return
    fi

    local total=0 min=${times[0]} max=${times[0]}
    for t in "${times[@]}"; do
        total=$((total + t))
        [ $t -lt $min ] && min=$t
        [ $t -gt $max ] && max=$t
    done
    local avg=$((total / ${#times[@]}))

    if [ $failures -gt 0 ]; then
        printf "| %-28s | %6d | %6d | %6d | (%d/%d FAILED)\n" "$name" "$avg" "$min" "$max" "$failures" "$ITERATIONS"
    else
        printf "| %-28s | %6d | %6d | %6d |\n" "$name" "$avg" "$min" "$max"
    fi
}

echo "======================================================================"
echo "End-to-end compilation (source -> object file, -c)"
echo "======================================================================"
echo ""
printf "| %-28s | %6s | %6s | %6s |\n" "Compiler" "Avg ms" "Min ms" "Max ms"
printf "| %-28s | %6s | %6s | %6s |\n" "----------------------------" "------" "------" "------"

if command -v clang++ &> /dev/null; then
    benchmark "Clang++ -O0 (default)" clang++ -std=c++20 -O0 "$TEST_FILE" -c -o /tmp/bench.o
    benchmark "Clang++ -O2" clang++ -std=c++20 -O2 "$TEST_FILE" -c -o /tmp/bench.o
fi

if command -v g++ &> /dev/null; then
    benchmark "GCC -O0 (default)" g++ -std=c++20 -O0 "$TEST_FILE" -c -o /tmp/bench.o
    benchmark "GCC -O2" g++ -std=c++20 -O2 "$TEST_FILE" -c -o /tmp/bench.o
fi

[ $HAS_DEBUG -eq 1 ] && benchmark "FlashCpp (debug, -g)" "$FLASHCPP_DEBUG" "$TEST_FILE" -o /tmp/bench.o
[ $HAS_RELEASE -eq 1 ] && benchmark "FlashCpp (release, -O3)" "$FLASHCPP_RELEASE" "$TEST_FILE" -o /tmp/bench.o

echo ""

echo "======================================================================"
echo "Parse-only (frontend only, no codegen)"
echo "======================================================================"
echo ""
printf "| %-28s | %6s | %6s | %6s |\n" "Compiler" "Avg ms" "Min ms" "Max ms"
printf "| %-28s | %6s | %6s | %6s |\n" "----------------------------" "------" "------" "------"

if command -v clang++ &> /dev/null; then
    benchmark "Clang++ -fsyntax-only" clang++ -std=c++20 -fsyntax-only "$TEST_FILE"
fi
if command -v g++ &> /dev/null; then
    benchmark "GCC -fsyntax-only" g++ -std=c++20 -fsyntax-only "$TEST_FILE"
fi
echo ""
echo "(FlashCpp does not have a parse-only mode)"
echo ""

# Show FlashCpp internal timing if debug build exists
if [ $HAS_DEBUG -eq 1 ]; then
    echo "======================================================================"
    echo "FlashCpp internal timing breakdown (debug build)"
    echo "======================================================================"
    echo ""
    "$FLASHCPP_DEBUG" "$TEST_FILE" -o /tmp/bench.o 2>&1 | grep -E "^(Phase|Preproc|Lexer|Pars|IR Conv|Deferred|Code Gen|Other|TOTAL|---)" || true
    echo ""
fi

# Cleanup
rm -f /tmp/bench.o
