// Test constexpr destructors (C++20 feature)

// Simple test: destructor can be marked constexpr
struct Counter {
    int value;

    constexpr Counter(int v) : value(v) {}
    
    // Constexpr destructor - in practice, doesn't do much at compile time
    // but allows the type to be used in constexpr contexts
    constexpr ~Counter() {
        // Empty destructor - valid in constexpr
    }
};

// Test that constexpr destructors allow objects in constant expressions
constexpr int test_basic() {
    Counter c(42);
    return c.value;
}

// Note: The destructor being constexpr allows Counter to be used
// in constexpr contexts. The actual destructor call at end of scope
// is a no-op at compile time.
static_assert(test_basic() == 42, "Basic test should return 42");

int main() {
    return 0;
}

