// Comprehensive test for template partial specializations
// Tests pointer, reference, const, and combinations

template<typename T>
struct Traits {
    static constexpr int kind = 0;  // Primary
};

// Pointer specialization
template<typename T>
struct Traits<T*> {
    static constexpr int kind = 1;  // Pointer
};

// Lvalue reference specialization
template<typename T>
struct Traits<T&> {
    static constexpr int kind = 2;  // Lvalue ref
};

// Rvalue reference specialization
template<typename T>
struct Traits<T&&> {
    static constexpr int kind = 3;  // Rvalue ref
};

// Const specialization
template<typename T>
struct Traits<const T> {
    static constexpr int kind = 4;  // Const
};

// Const pointer specialization
template<typename T>
struct Traits<const T*> {
    static constexpr int kind = 5;  // Const pointer
};

// Const reference specialization
template<typename T>
struct Traits<const T&> {
    static constexpr int kind = 6;  // Const lvalue ref
};

int main() {
    // Test primary template
    if (Traits<int>::kind != 0) return 1;
    
    // Test pointer specialization
    if (Traits<int*>::kind != 1) return 2;
    
    // Test lvalue reference specialization
    if (Traits<int&>::kind != 2) return 3;
    
    // Test rvalue reference specialization
    if (Traits<int&&>::kind != 3) return 4;
    
    // Test const specialization
    if (Traits<const int>::kind != 4) return 5;
    
    // Test const pointer specialization
    if (Traits<const int*>::kind != 5) return 6;
    
    // Test const reference specialization
    if (Traits<const int&>::kind != 6) return 7;
    
    return 0;
}
