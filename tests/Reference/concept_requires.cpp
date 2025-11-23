// Test file for C++20 requires clauses on templates
// Testing: template<typename T> requires constraint

// Simple concept for testing
template<typename T>
concept Integral = true;

// Function template with simple requires clause (just concept name)
template<typename T>
requires true
T add(T a, T b) {
    return a + b;
}

int main() {
    int x = add(5, 10);
    return x;
}
