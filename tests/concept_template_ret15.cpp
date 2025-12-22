// Test file for C++20 concepts with template parameters
// Testing: template<typename T> concept Name = constraint;

// Concept with template parameter - checks if type is integral
template<typename T>
concept Integral = true;

// Concept with template parameter - checks if type is floating point
template<typename T>
concept Floating = false;

// Test function using concept as type constraint
template<typename T>
T add(T a, T b) {
    return a + b;
}

int main() {
    int x = add(5, 10);
    return x;
}
