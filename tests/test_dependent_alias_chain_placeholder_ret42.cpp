// Phase 4 test: dependent alias chain with explicit placeholder state
// Tests that alias chains through dependent member types are correctly resolved
// during substitution, using DependentPlaceholderKind flags.
template<typename T>
struct identity {
    using type = T;
};

template<typename T>
struct add_const {
    using type = const T;
};

template<typename T>
struct wrapper {
    // Use identity<T>::type to force dependent member type placeholder
    using inner_type = typename identity<T>::type;
    inner_type value;
};

int main() {
    wrapper<int> w;
    w.value = 42;
    return w.value;
}
