// Test void_t SFINAE pattern - POSITIVE case (type WITH 'type' member)
// 
// KNOWN LIMITATION (as of December 2024):
// This test documents a known limitation in FlashCpp's void_t SFINAE implementation.
// 
// The issue: FlashCpp correctly handles the NEGATIVE case (type WITHOUT 'type' member)
// but does NOT correctly handle the POSITIVE case (type WITH 'type' member).
// 
// Root cause: Pattern matching happens BEFORE default template arguments are filled in.
// When has_type<WithType> is instantiated:
// - We have 1 argument: WithType
// - Pattern has 2 arguments: T, void_t<typename T::type>
// - Size mismatch → pattern doesn't match → primary template used
//
// Expected behavior: has_type<WithType>::value should be true (specialization matches)
// Actual behavior: has_type<WithType>::value is false (primary template used)
//
// See tests/std/STANDARD_HEADERS_MISSING_FEATURES.md for full details.
// 
// This test returns:
// - 42 if behavior matches current (incorrect) implementation
// - 0 if behavior is fixed (correct)
//
// TODO: When void_t SFINAE is fixed, this test should be updated to expect return 0
// and renamed to test_void_t_positive_ret0.cpp

template<typename...>
using void_t = void;

struct false_type {
    static constexpr bool value = false;
};

struct true_type {
    static constexpr bool value = true;
};

// Primary template - default case (no member 'type')
template<typename T, typename = void>
struct has_type : false_type {};

// Specialization - has a nested 'type' member
template<typename T>
struct has_type<T, void_t<typename T::type>> : true_type {};

// Test type WITH 'type' member
struct WithType {
    using type = int;
};

int main() {
    // Test: Type WITH nested 'type' member
    // Expected (correct): should return true (specialization matches)
    // Actual (known bug): returns false (primary template used due to pattern matching limitation)
    bool has_member_with = has_type<WithType>::value;
    
    // Return 42 if current (incorrect) behavior - primary template used
    // Return 0 if fixed (correct) behavior - specialization matches
    return has_member_with ? 0 : 42;
}
