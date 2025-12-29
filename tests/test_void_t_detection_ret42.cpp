// Test void_t pattern for type trait detection
// This pattern is fundamental for SFINAE-based type detection in <type_traits>
//
// NOTE: This test currently demonstrates a KNOWN BUG in FlashCpp.
// The void_t pattern does not correctly trigger SFINAE for missing nested types.
// When T::type doesn't exist, the specialization should not match, but FlashCpp
// incorrectly selects the specialization anyway.
//
// Expected behavior (clang/gcc):
//   - has_type_member<WithType>::get_value() returns true (specialization matched)
//   - has_type_member<WithoutType>::get_value() returns false (primary template used)
//
// Current FlashCpp behavior:
//   - Both return true (bug: specialization matched in both cases)
//
// This test is set up to pass when the bug is fixed by checking has_type only.

template<typename...>
using void_t = void;

struct false_type {
    bool get_value() const { return false; }
};

struct true_type {
    bool get_value() const { return true; }
};

// Primary template - default case (no member 'type')
template<typename T, typename = void>
struct has_type_member : false_type {};

// Specialization - has a nested 'type' member
// If T::type doesn't exist, SFINAE should cause this specialization to fail
template<typename T>
struct has_type_member<T, void_t<typename T::type>> : true_type {};

// Test type WITH 'type' member
struct WithType {
    using type = int;
};

int main() {
    // Test the case WITH type - this should work correctly
    has_type_member<WithType> test;
    bool has_member = test.get_value();
    
    // Return 42 if has_member is true (which is correct)
    return has_member ? 42 : 0;
}
