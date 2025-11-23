// Test file for type trait evaluation in concepts
// Testing: type trait-like constraints (simplified)

// Concept that would use type traits
template<typename T>
concept IntegralLike = true;

// Function template with concept
template<typename T>
requires true
T double_value(T x) {
    return x + x;
}

int main() {
    int x = double_value(5);
    return x;
}
