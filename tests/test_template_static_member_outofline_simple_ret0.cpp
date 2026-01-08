// Test static member variable definition outside template class body
// This test verifies parsing support for the pattern, even though
// in C++17+ the out-of-class definition is optional for constexpr members

template<typename T>
struct Container {
    static constexpr int value = 42;
};

// Out-of-class definition of static member (provides storage in C++11/14)
// In C++17+, this is optional but still valid
template<typename T>
constexpr int Container<T>::value;

int main() {
    // For now, just test that it compiles and doesn't crash
    // The actual value retrieval will work once static member
    // instantiation is fully implemented
    return 0;
}
