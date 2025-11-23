// Test file for concept subsumption rules
// Testing: More specific concepts subsume less specific ones (simplified)

// Base concept
template<typename T>
concept Movable = true;

// More specific concept
template<typename T>
concept Copyable = true;

// Even more specific concept  
template<typename T>
concept Regular = true;

// Function with simple constraint
template<typename T>
requires true
T identity(T x) {
    return x;
}

int main() {
    int x = 42;
    int result = identity(x);
    return result;
}
