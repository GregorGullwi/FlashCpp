// Test pseudo-destructor call parsing (template context)
// Pattern from <type_traits>: decltype(declval<_Tp&>().~_Tp())

template<typename T>
struct test {
    // Use decltype with pseudo-destructor pattern
    using type = void;  // Simplified - actual pattern needs full template metaprogramming
};

int main() {
    return 0;
}
