// Test: Function call followed by member access in constant expressions
// Impact: Unblocks <type_traits>, <exception>, <utility>, <tuple>, <span>, <array>, and many more

template<typename T>
struct __type_identity {
    using type = T;
};

struct true_type {
    static constexpr bool value = true;
};

template <typename _Tp>
constexpr true_type check(__type_identity<_Tp>) {
    return {};
}

int test_in_function() {
    auto result = check(__type_identity<int>{});
    return result.value ? 0 : 1;
}

struct TestStruct {
    static constexpr bool value = check(__type_identity<int>{}).value;
    static_assert(check(__type_identity<int>{}).value, "test");
};

int main() {
    return test_in_function();
}

