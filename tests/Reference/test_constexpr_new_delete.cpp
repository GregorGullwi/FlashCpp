// Test C++20 constexpr new and delete

struct Counter {
    int value;
    
    constexpr Counter(int v) : value(v) {}
    constexpr ~Counter() {}
};

// Test 1: Simple allocation and deallocation (no dereference yet)
constexpr int test_simple_new_delete() {
    int* p = new int(42);
    delete p;
    return 1;  // Success if no leak error
}

static_assert(test_simple_new_delete() == 1, "Simple new/delete should work");

int main() {
    return 0;
}
