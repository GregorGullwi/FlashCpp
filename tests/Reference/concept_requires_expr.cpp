// Test file for C++20 requires expressions
// Testing: requires { requirements; }

// Simple requires expression concept (simplified)
template<typename T>
concept AlwaysValid = requires {
    true;  // Simple requirement: always true
};

// Function using the concept
template<typename T>
T identity(T a) {
    return a;
}

int main() {
    int x = identity(5);
    return x;
}
