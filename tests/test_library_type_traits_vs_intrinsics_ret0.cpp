// Test that library type traits like __is_swappable are distinguished from compiler intrinsics
// This tests that identifiers starting with __is_ or __has_ followed by < are treated as
// template classes (library type traits) rather than compiler intrinsics like __is_void()

// Mimic standard library's __is_swappable (template class, not intrinsic)
template<typename T>
struct __is_swappable {
    static constexpr bool value = true;
};

// Mimic standard library's __is_nothrow_swappable (template class)
template<typename T>
struct __is_nothrow_swappable {
    static constexpr bool value = false;
};

// Mimic standard library's __has_unique_object_repr (template class)
template<typename T>
struct __has_unique_object_repr {
    static constexpr bool value = true;
};

// Test using library type trait with template syntax
template<typename T>
bool check_swappable() {
    return __is_swappable<T>::value;
}

// Also test compiler intrinsics still work (followed by parentheses)
bool test_compiler_intrinsic() {
    // __is_void is a compiler intrinsic - uses parentheses
    return __is_void(void);
}

int main() {
    // Test library type traits (template classes)
    bool is_swappable = __is_swappable<int>::value;
    bool is_nothrow = __is_nothrow_swappable<int>::value;
    bool has_unique = __has_unique_object_repr<int>::value;
    
    // Test via template function
    bool via_template = check_swappable<int>();
    
    // Test compiler intrinsic (should still work)
    bool intrinsic_result = test_compiler_intrinsic();
    
    // Return 0 if all expected values are correct
    if (is_swappable && !is_nothrow && has_unique && via_template && intrinsic_result) {
        return 0;
    }
    return 1;
}
