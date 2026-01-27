// Test for return-type-requirement syntax in requires expressions
// Syntax: { expression } -> ConceptName

// Simple concept to use in return-type-requirement
template<typename T>
concept Integral = true;

// Concept with return-type-requirement (no parameters needed)
template<typename T>
concept HasValue = requires {
    { true } -> Integral;  // Return type must satisfy Integral concept
};

// Another test with different syntax
template<typename T>
concept Multiple = requires {
    { true };              // No return type constraint
    { false } -> Integral;  // With return type constraint
};

// Test with multiple return-type-requirements
template<typename T>
concept Advanced = requires {
    { true };               // Simple compound requirement
    { false } -> Integral;  // Compound with return type
    typename T;             // Type requirement
    requires true;          // Nested requirement
};

int main() {
    return 0;
}
