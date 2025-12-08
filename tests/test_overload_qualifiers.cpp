// Test function overloading with different type qualifiers
// This test verifies that the compiler correctly distinguishes between:
// - value, reference, rvalue reference, and const reference parameters
// - const and non-const qualifiers on base types

// Test 1: Different reference qualifiers
void test_ref(int x) { }        // By value
void test_ref(int& x) { }       // By lvalue reference
void test_ref(int&& x) { }      // By rvalue reference
void test_ref(const int& x) { } // By const lvalue reference

// Test 2: Const qualifiers on pointers at different levels
void test_ptr(int* p) { }             // Pointer to int
void test_ptr(const int* p) { }       // Pointer to const int

// Test 3: Multiple parameters with different qualifiers
void test_multi(int a, int& b) { }
void test_multi(int& a, int b) { }
void test_multi(int a, int b) { }

// Test 4: Namespace-scoped overloads
namespace A {
    void test_ns(int x) { }
    void test_ns(int& x) { }
}

namespace B {
    void test_ns(int x) { }  // Different namespace, should be allowed
}

int main() {
    return 0;
}
