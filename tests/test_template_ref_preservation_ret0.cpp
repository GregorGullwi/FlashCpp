// Test case for template argument reference preservation
// Pattern from README_STANDARD_HEADERS.md:
// When instantiating function templates, reference qualifiers on template arguments
// should be properly preserved when passed to nested variable templates.

namespace ns {
    // Primary template: returns false for non-references
    template<typename T>
    inline constexpr bool is_reference_v = false;

    // Partial specialization: returns true for lvalue references
    template<typename T>
    inline constexpr bool is_reference_v<T&> = true;

    // Partial specialization: returns true for rvalue references
    template<typename T>
    inline constexpr bool is_reference_v<T&&> = true;

    // Test function template that uses is_reference_v
    template<typename T>
    constexpr bool test_is_ref() {
        return is_reference_v<T>;
    }
}

int main() {
    // Direct use of variable template
    constexpr bool direct_nonref = ns::is_reference_v<int>;     // false
    constexpr bool direct_lref = ns::is_reference_v<int&>;      // true
    constexpr bool direct_rref = ns::is_reference_v<int&&>;     // true

    // Use through function template - reference preservation must work
    constexpr bool func_nonref = ns::test_is_ref<int>();        // should be false
    constexpr bool func_lref = ns::test_is_ref<int&>();         // should be true
    constexpr bool func_rref = ns::test_is_ref<int&&>();        // should be true

    // Check all values
    // If reference preservation works: 0 + 1 + 1 + 0 + 1 + 1 = 4
    int result = direct_nonref + direct_lref + direct_rref + 
                 func_nonref + func_lref + func_rref;

    // Return 0 if correct (result should be 4)
    return (result == 4) ? 0 : result;
}
