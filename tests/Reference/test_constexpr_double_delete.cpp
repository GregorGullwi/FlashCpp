// Test C++20 constexpr new/delete - double delete detection

// Test 1: Double delete should cause compile error
constexpr int test_double_delete() {
    int* p = new int(42);
    delete p;
    delete p;  // Double delete - should error
    return 1;
}

// This should fail to compile due to double delete
static_assert(test_double_delete() == 1, "Should detect double delete");

int main() {
    return 0;
}
