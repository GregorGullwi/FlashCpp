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
    constexpr bool direct_nonref = ns::is_reference_v<int>;     // false (0)
    constexpr bool direct_lref = ns::is_reference_v<int&>;      // true (1)
    constexpr bool direct_rref = ns::is_reference_v<int&&>;     // true (1)

    // Use through function template - reference preservation must work
    constexpr bool func_nonref = ns::test_is_ref<int>();        // false (0)
    constexpr bool func_lref = ns::test_is_ref<int&>();         // true (1)
    constexpr bool func_rref = ns::test_is_ref<int&&>();        // true (1)

    // Calculate total: direct_nonref(0) + direct_lref(1) + direct_rref(1) +
    //                  func_nonref(0) + func_lref(1) + func_rref(1) = 4
    constexpr int EXPECTED_TOTAL = 4;  // 2 direct refs + 2 function template refs
    
    int result = direct_nonref + direct_lref + direct_rref + 
                 func_nonref + func_lref + func_rref;

    // Return 0 if all reference types were correctly detected
    return (result == EXPECTED_TOTAL) ? 0 : result;
}
