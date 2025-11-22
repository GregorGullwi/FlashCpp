// Test C++20 constexpr new/delete - memory leak detection

// Test 1: Memory leak should cause compile error
constexpr int test_memory_leak() {
    int* p = new int(42);
    // Forgot to delete - should error
    return 1;
}

// This should fail to compile due to memory leak
static_assert(test_memory_leak() == 1, "Should detect memory leak");

int main() {
    return 0;
}
