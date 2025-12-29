// Test void_t pattern for SFINAE detection with default template arguments
// This test validates that types WITHOUT a nested 'type' member correctly use the primary template.
//
// The void_t pattern works as follows:
// - Primary template: template<typename T, typename = void> struct has_type : false_type {};
// - Specialization: template<typename T> struct has_type<T, void_t<typename T::type>> : true_type {};
//
// CURRENT STATUS:
// - has_type<WithoutType> correctly returns false (WORKING)
// - has_type<WithType> currently returns false (KNOWN LIMITATION - see below)
//
// KNOWN LIMITATION:
// The positive case (has_type<WithType> returning true) requires proper SFINAE pattern
// matching where the specialization pattern is matched with default arguments filled in.
// Currently, pattern matching happens BEFORE default argument substitution, so:
// - has_type<WithType> → template_args = [WithType] (1 arg)
// - pattern = [T, void] (2 args after alias expansion)
// - Size mismatch → no pattern match → primary template used
//
// BUG FIXED: Previously, the negative case (WithoutType) was broken because:
// 1. void_t was mangled as "unknown" instead of "void" in template names
// 2. GlobalLoad was using 32-bit MOV for 8-bit bool values, loading garbage
// 3. Conditional branches were loading 32 bits when checking 8-bit bool conditions

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

// Test type WITHOUT 'type' member
struct WithoutType {
    int value;
};

int main() {
    // Test: Type WITHOUT nested 'type' member should return false
    // This validates SFINAE correctly rejects the specialization
    bool has_member_without = has_type<WithoutType>::value;
    
    // Return 42 if WithoutType correctly has no 'type' member detected
    // Return 0 if bug (incorrectly detected as having 'type')
    return has_member_without ? 0 : 42;
}
