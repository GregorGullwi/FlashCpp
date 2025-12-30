// Test void_t SFINAE pattern - POSITIVE case (type WITH 'type' member)
// 
// This test validates that FlashCpp correctly handles void_t SFINAE patterns.
// 
// Expected behavior: has_type<WithType>::value should be true (specialization matches)
// 
// This test returns:
// - 0 if behavior is correct (specialization matches for type with ::type member)
// - 42 if behavior is incorrect (primary template used)

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
    // Expected: should return true (specialization matches)
    bool has_member_with = has_type<WithType>::value;
    
    // Return 0 if correct behavior - specialization matches
    // Return 42 if incorrect behavior - primary template used
    return has_member_with ? 0 : 42;
}
