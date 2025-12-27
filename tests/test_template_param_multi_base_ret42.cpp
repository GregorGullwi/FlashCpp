// Test: Multiple template parameters as base classes
// This demonstrates that multiple template parameters can be used as bases

struct A {
    int a = 10;
};

struct B {
    int b = 20;
};

// Multiple template parameters as base classes
template<typename T, typename U>
struct Multi : T, U {
};

// Instantiate with two concrete types
Multi<A, B> m;

int main() {
    // The struct should successfully inherit from both A and B
    // Just verify compilation succeeds
    return 42;
}
