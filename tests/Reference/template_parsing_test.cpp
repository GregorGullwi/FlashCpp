// Template Parsing Test - Phase 1 Complete
// This file tests that the FlashCpp compiler can successfully parse
// various template function declarations.
//
// NOTE: Template instantiation is NOT yet implemented (Phase 2).
// This test only verifies that template syntax is correctly parsed.

// 1. Basic type parameter with typename keyword
template<typename T>
T identity(T x);

// 2. Basic type parameter with class keyword
template<class T>
T copy(T x);

// 3. Multiple type parameters
template<typename T, typename U>
T convert(U value);

// 4. Multiple type parameters with mixed keywords
template<class T, typename U, class V>
V combine(T a, U b);

// 5. Non-type template parameter (integer)
template<int N>
int multiply_by_constant(int x);

// 6. Non-type template parameter (bool)
template<bool Flag>
int conditional_value();

// 7. Mixed type and non-type parameters
template<typename T, int Size>
T get_element(int index);

// 8. Template function with concrete return type
template<typename T>
int get_size(T container);

// 9. Template function with concrete parameter types
template<typename T>
T process_int(int value);

// 10. Template function with multiple concrete parameters
template<typename T>
T combine_values(int a, double b, char c);

int main() {
    // Template instantiation will be tested in Phase 2
    // For now, we just verify that parsing works
    return 0;
}

