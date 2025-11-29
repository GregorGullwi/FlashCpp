// Test file for template specialization parsing
// This tests the ability to parse template<> specialization syntax

template<typename T> struct MyType { };

// Explicit specialization - requires template<> prefix
template<> struct MyType<int> { using type = int; };
template<> struct MyType<bool> { using type = bool; };
template<> struct MyType<char> { using type = char; };

int main() { return 0; }
