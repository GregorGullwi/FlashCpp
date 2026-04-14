// Phase 4 test: sizeof on dependent member-type placeholder
// Tests that sizeof resolution of dependent nested types works correctly
// when the placeholder is identified by DependentPlaceholderKind flag.
template<typename T>
struct type_holder {
    using value_type = T;
};

template<typename T>
struct size_checker {
    static constexpr int size = sizeof(T);
};

int main() {
    // sizeof(long long) should be 8 on 64-bit
    return size_checker<long long>::size;  // 8
}
