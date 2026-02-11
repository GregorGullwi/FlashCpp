// Test: Self-referential template struct (like __ratio_add_impl in <ratio>)
// The primary template references itself with different template arguments in its body.

template<typename T, bool = (T::value >= 0)>
struct recursive_impl {
    // Self-reference with different args: same pattern as __ratio_add_impl
    typedef typename recursive_impl<T, true>::type negated;
    typedef T type;
};

template<typename T>
struct recursive_impl<T, true> {
    typedef T type;
};

struct positive_val {
    static constexpr int value = 42;
};

int main() {
    // The specialization should be selected since value >= 0
    using result = recursive_impl<positive_val>::type;
    return result::value - 42;  // Should return 0
}
