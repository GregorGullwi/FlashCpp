// Test constexpr destructors (C++20 feature)

struct Counter {
    int value;

    constexpr Counter(int v) : value(v) {}
    
    constexpr ~Counter() {
        // In a constexpr context, the destructor can modify the object
        // before it's destroyed
        value = 0;
    }
};

// Test that constexpr destructors work in constant expressions
constexpr int test_destructor() {
    Counter c(42);
    // When c goes out of scope, its destructor should be called
    return c.value;  // Should return 42 (before destruction)
}

static_assert(test_destructor() == 42, "Destructor test should return 42");

// Test with multiple objects
constexpr int test_multiple_destructors() {
    Counter c1(10);
    Counter c2(20);
    int sum = c1.value + c2.value;
    // Both destructors should be called when they go out of scope
    return sum;  // Should return 30
}

static_assert(test_multiple_destructors() == 30, "Multiple destructor test should return 30");

// Test with nested scopes
constexpr int test_nested_scopes() {
    Counter c1(100);
    int result = c1.value;
    {
        Counter c2(50);
        result += c2.value;
        // c2's destructor called here
    }
    // Only c1's destructor should be called at the end
    return result;  // Should return 150
}

static_assert(test_nested_scopes() == 150, "Nested scope test should return 150");

int main() {
    return 0;
}
