// Test for std::move support requiring:
// 1. Universal reference (T&&) parameter deduction with reference collapsing
// 2. Template return type with typename support  
// 3. Proper mangled name handling for template instantiations

namespace std {
    // Remove reference helper
    template<typename T> struct remove_reference      { using type = T; };
    template<typename T> struct remove_reference<T&>  { using type = T; };
    template<typename T> struct remove_reference<T&&> { using type = T; };
    
    // std::move - template function with typename return type and universal reference parameter
    template<typename T>
    typename remove_reference<T>::type&& move(T&& arg) {
        using ReturnType = typename remove_reference<T>::type&&;
        return static_cast<ReturnType>(arg);
    }
}

// Test function that takes rvalue reference
int consume(int&& x) {
    return x + 10;
}

int main() {
    int value = 5;
    
    // Test using std::move - should instantiate std::move<int&>
    // T deduced as int& (lvalue reference)
    // remove_reference<int&>::type is int
    // Return type is int&&
    int result = consume(std::move(value));
    
    return (result == 15) ? 0 : 1;  // 5 + 10 = 15
}
