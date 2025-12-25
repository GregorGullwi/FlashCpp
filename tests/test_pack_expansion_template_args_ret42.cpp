// Test parameter pack expansion in template arguments
// This tests the pattern from <type_traits> line 175

template<bool... Vals>
struct all_true {
    static constexpr bool value = (Vals && ...);
};

template<typename T>
struct has_value {
    static constexpr bool value = true;
};

// Test with parameter pack - pattern similar to <type_traits>
template<typename... Bn>
struct test {
    // Using pack expansion in template argument
    static constexpr bool result = all_true<!bool(Bn::value)...>::value;
};

int main() {
    // Test with no types - should give true (all true with empty pack)
    // In reality, we just test that it compiles
    return 42;
}
