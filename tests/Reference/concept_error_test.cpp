// Test file for C++20 constraint error messages
// This file intentionally has constraint violations to test error reporting

// Function with a constraint that will fail (literal false)
template<typename T>
requires false
T bad_constraint(T x) {
    return x;
}

int main() {
    // This should trigger constraint error for false literal
    int x = bad_constraint(5);
    
    return x;
}
