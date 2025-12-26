// Test for namespace-qualified template lookup
// This addresses the __detail::__or_fn pattern

namespace detail {
    template<typename T>
    struct helper {
        static constexpr int value = 42;
    };
}

template<typename T>
int get_value() {
    return detail::helper<T>::value;
}

int main() {
    return get_value<int>();  // Should return 42
}
