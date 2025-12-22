// Test file for C++20 concepts with requires expressions
// Testing: requires(T a, T b) { a + b; }

// Concept with requires expression that checks addability
template<typename T>
concept Addable = requires(T a, T b) {
    a + b;
};

// Function template constrained by the Addable concept
template<typename T>
requires Addable<T>
T add(T a, T b) {
    return a + b;
}

// Simple function to test with integer types
int main() {
    int x = add(5, 10);
    return x;
}
