// Comprehensive test for pointer-to-member operators (.* and ->*) and type declarations
// Tests parsing (not runtime execution due to code generation limitations)

template<typename T>
T declval();

struct Point {
    int x;
    int y;
};

// ===== Part 1: Type Declarations =====

// Function returning pointer-to-member type
int Point::*getPtrToMember() {
    return nullptr;
}

// ===== Part 2: Template/Decltype Contexts (from type_traits patterns) =====

// Test .* operator in decltype context
template<typename T, typename M>
using member_access_type = decltype(declval<T>().*declval<M>());

// Test complex patterns with pack expansion
template<typename Tp>
struct result_success {
    using type = Tp;
};

struct test_patterns {
    // Simple .* in template argument
    template<typename Fp, typename Tp1>
    static result_success<decltype(declval<Tp1>().*declval<Fp>())> test_simple(int);
    
    // Complex: .* with function call and pack expansion (type_traits line 2499 pattern)
    template<typename Fp, typename Tp1, typename... Args>
    static result_success<decltype((declval<Tp1>().*declval<Fp>())(declval<Args>()...))> test_complex(int);
};

// ===== Part 3: Runtime Usage Tests =====

int main() {
    // Test 1: Basic pointer-to-member type declaration
    int Point::*ptr_basic;
    
    // Test 2: Initialization from function
    int Point::*ptr_from_func = getPtrToMember();
    
    // Test 3: Initialization with nullptr
    int Point::*ptr_null = nullptr;
    
    // Test 4: Address-of-member
    int Point::*ptr_to_x = &Point::x;
    
    // Test 5: Object creation
    Point p = {10, 32};
    
    // Test 6: Runtime .* operator (basic validation - codegen creates IR but may not execute correctly)
    p.*ptr_null;
    
    return 0;
}
