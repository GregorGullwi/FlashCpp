#!/bin/bash
# Performance testing script for standard headers
# Tests headers with different timeouts, build modes, and log levels

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
cd "$REPO_ROOT"

# Include paths
INCLUDE_PATHS="-I/usr/lib/gcc/x86_64-linux-gnu/14/../../../../include/c++/14 -I/usr/lib/gcc/x86_64-linux-gnu/14/../../../../include/x86_64-linux-gnu/c++/14 -I/usr/lib/llvm-18/lib/clang/18/include"

# Test configurations
CONFIGS=(
    "release:x64/Release/FlashCpp:300"
    "debug:x64/Debug/FlashCpp:300"
)

# Headers to test (known to timeout)
HEADERS=(
    "concepts"
    "utility"
    "string_view"
    "string"
    "vector"
    "optional"
    "tuple"
    "memory"
)

echo "==========================================="
echo "Standard Header Performance Test"
echo "==========================================="
echo ""
echo "Testing $(date)"
echo ""

for config in "${CONFIGS[@]}"; do
    IFS=':' read -r name binary timeout <<< "$config"
    
    echo "==========================================="
    echo "Configuration: $name"
    echo "Binary: $binary"
    echo "Timeout: ${timeout}s"
    echo "==========================================="
    echo ""
    
    for header in "${HEADERS[@]}"; do
        test_file="tests/std/test_std_${header}.cpp"
        
        if [ ! -f "$test_file" ]; then
            echo "[$header] SKIP - test file not found"
            continue
        fi
        
        echo -n "[$header] Testing... "
        
        # Run with time measurement
        start_time=$(date +%s.%N)
        timeout $timeout ./$binary "$test_file" $INCLUDE_PATHS > /tmp/flashcpp_perf_${header}.log 2>&1
        exit_code=$?
        end_time=$(date +%s.%N)
        
        elapsed=$(echo "$end_time - $start_time" | bc)
        
        if [ $exit_code -eq 124 ]; then
            echo "TIMEOUT (>${timeout}s)"
        elif [ $exit_code -eq 0 ]; then
            # Check if object file was created
            obj_file="test_std_${header}.obj"
            if [ -f "$obj_file" ]; then
                echo "SUCCESS (${elapsed}s)"
                rm -f "$obj_file"
            else
                echo "FAILED (${elapsed}s) - no object file"
                tail -5 /tmp/flashcpp_perf_${header}.log | head -3
            fi
        else
            echo "ERROR (${elapsed}s, exit=$exit_code)"
            tail -5 /tmp/flashcpp_perf_${header}.log | head -3
        fi
    done
    
    echo ""
done

echo "==========================================="
echo "Testing with different log levels (Release)"
echo "==========================================="
echo ""

LOG_LEVELS=("error" "warning")

for log_level in "${LOG_LEVELS[@]}"; do
    echo "--- Log Level: $log_level ---"
    
    for header in "concepts" "utility" "string"; do
        test_file="tests/std/test_std_${header}.cpp"
        
        if [ ! -f "$test_file" ]; then
            continue
        fi
        
        echo -n "[$header] "
        
        start_time=$(date +%s.%N)
        timeout 300 ./x64/Release/FlashCpp "$test_file" $INCLUDE_PATHS --log-level=$log_level > /tmp/flashcpp_log_${header}.log 2>&1
        exit_code=$?
        end_time=$(date +%s.%N)
        
        elapsed=$(echo "$end_time - $start_time" | bc)
        
        if [ $exit_code -eq 124 ]; then
            echo "TIMEOUT (>300s)"
        elif [ $exit_code -eq 0 ]; then
            obj_file="test_std_${header}.obj"
            if [ -f "$obj_file" ]; then
                echo "SUCCESS (${elapsed}s)"
                rm -f "$obj_file"
            else
                echo "FAILED (${elapsed}s)"
            fi
        else
            echo "ERROR (${elapsed}s)"
        fi
    done
    echo ""
done

echo "==========================================="
echo "Testing completed"
echo "==========================================="
