// Test file for template partial specialization parsing
// This tests various template specialization patterns found in standard library headers

// Basic template
template<typename T> struct MyType { };

// Explicit (full) specialization - empty template params, specific type in angle brackets
template<> struct MyType<int> { using type = int; };

// Partial specialization - has template params but specializes on a pattern
template<typename T> struct MyType<const T> { };
template<typename T> struct MyType<T*> { };

// CURRENTLY FAILING: Partial specialization with inheritance
// This pattern is used in <cstddef> for __byte_operand
// template<typename T> struct MyType<const T> : MyType<T> { };

int main() { return 0; }
