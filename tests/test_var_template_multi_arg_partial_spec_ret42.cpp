// Test: multi-argument variable template partial specialization
// Pattern: pair_trait<T&, U*> should match pair_trait<int&, float*>
template<typename T, typename U>
constexpr int pair_trait = 0;

// Partial specialization for <T&, U*>
template<typename T, typename U>
constexpr int pair_trait<T&, U*> = 42;

int main() {
    static_assert(pair_trait<int&, float*> == 42, "should match partial spec");
    return pair_trait<int&, float*>;  // Expected: 42
}
