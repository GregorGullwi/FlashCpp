// Test that non-type template parameters are recognized in complex return types
// This tests the fix for patterns like: typename tuple_element<_Int, pair<_Tp1, _Tp2>>::type&
// The non-type template parameter _Int needs to be visible when parsing the return type.

// Simplified version of tuple_element pattern
template<int N, typename T>
struct element_type {
    using type = T;
};

// Function template with non-type template parameter in return type
// Pattern: template<int N, typename T1, typename T2> typename element_type<N, T1>::type get(...)
template<int _Int, typename _Tp1, typename _Tp2>
constexpr typename element_type<_Int, _Tp1>::type
get_element() {
    // When called as get_element<0, int, float>(), the return type is element_type<0, int>::type = int
    // So returning 0 is valid since int{} is 0
    return 0;
}

int main() {
    // Call with int as first type parameter - return type becomes int
    int result = get_element<0, int, float>();
    return result;  // Should return 0
}
