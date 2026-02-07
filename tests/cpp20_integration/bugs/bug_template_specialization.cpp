// Bug: Explicit template specialization produces incorrect results
// Status: RUNTIME FAILURE - Specialization not selected at runtime
// Date: 2026-02-07
//
// Explicit template specialization (template<>) should override the primary
// template for the specified type.

template<typename T>
T identity(T val) {
    return val;
}

template<>
int identity<int>(int val) {
    return val + 1;  // Specialized version adds 1
}

int main() {
    int result = identity(5);  // Should use specialization, return 6
    return result == 6 ? 0 : 1;
}

// Expected behavior (with clang++/g++):
// Compiles and runs successfully, returns 0 (result = 6)
//
// Actual behavior (with FlashCpp):
// Compiles and links, but returns 1 at runtime - the specialization
// is not being selected, and the primary template is used instead (result = 5).
//
// Fix: Ensure the template instantiation system checks for explicit
// specializations before instantiating the primary template.
