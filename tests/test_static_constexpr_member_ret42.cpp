// Test static constexpr member function evaluation
// This tests the fix for qualified static member function calls in constexpr context

struct Calculator {
    static constexpr int add(int a, int b) {
        return a + b;
    }
    
    static constexpr int multiply(int a, int b) {
        return a * b;
    }
};

// Test static_assert with static member function
static_assert(Calculator::add(5, 5) == 10, "add should work");
static_assert(Calculator::multiply(6, 7) == 42, "multiply should work");

// Test constexpr variable initialization
constexpr int result1 = Calculator::add(10, 20);
constexpr int result2 = Calculator::multiply(3, 4);

static_assert(result1 == 30, "result1 should be 30");
static_assert(result2 == 12, "result2 should be 12");

int main() {
    return result1 + result2;  // Should return 42
}
