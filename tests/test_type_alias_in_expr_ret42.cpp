// Test for type alias resolution in expressions
// This tests Issue 2 from the architecture plan

struct true_type {
    static constexpr int value = 42;
};

struct false_type {
    static constexpr int value = 0;
};

// Create type alias
using my_true = true_type;
using my_false = false_type;

int main() {
    // Test: Using type alias in variable declaration
    my_true t;
    
    return t.value;  // Should return 42
}
