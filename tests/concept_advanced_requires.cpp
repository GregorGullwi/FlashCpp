// Test file for advanced requires expression features (C++20)
// Testing: compound requirements, nested requirements, type requirements

// Concept with type requirement
template<typename T>
concept HasType = requires {
    typename T;     // Type requirement: T must be a valid type
};

// Concept with nested requirement
template<typename T>
concept WithNested = requires {
    requires true;      // Nested requirement (constraint within requires)
};

// Concept with compound requirement
template<typename T>
concept WithCompound = requires {
    { true };          // Compound requirement: expression in braces
};

// Concept combining all advanced requirement types
template<typename T>
concept Advanced = requires {
    typename T;         // Type requirement
    requires true;      // Nested requirement
    { true };           // Compound requirement
    true;               // Simple requirement
};

int main() {
    return 0;
}
