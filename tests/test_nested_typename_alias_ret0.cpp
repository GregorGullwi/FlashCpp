// Test nested typename type aliases
// Pattern: typename A<typename B<T>::type>::type

namespace std {
    template<typename T>
    struct remove_reference { using type = T; };
    
    template<typename T>
    struct remove_reference<T&> { using type = T; };
    
    template<typename T>
    struct remove_reference<T&&> { using type = T; };
    
    template<typename T>
    struct remove_cv { using type = T; };
    
    template<typename T>
    struct remove_cv<const T> { using type = T; };
    
    template<typename T>
    struct remove_cv<volatile T> { using type = T; };
    
    template<typename T>
    struct remove_cv<const volatile T> { using type = T; };
    
    // This is the problematic pattern
    template<typename _Tp>
    using __remove_cvref_t
        = typename remove_cv<typename remove_reference<_Tp>::type>::type;
}

int main() {
    using T1 = std::__remove_cvref_t<int>;
    using T2 = std::__remove_cvref_t<const int&>;
    return 0;
}
