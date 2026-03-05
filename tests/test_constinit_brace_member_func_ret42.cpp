// Test: non-operator() member function call on a brace-initialized constexpr object
// This tests the path where evaluate_member_function_call receives an InitializerListNode
// initializer for a non-operator() call (e.g., obj.getValue() instead of obj()).
struct Pair {
    int a;
    int b;
    constexpr int sum() const { return a + b; }
};

constexpr Pair p{20, 22};
constinit int x = p.sum();  // should be 42

int main() {
    return x;
}
