// Simplified test for the type_traits line 2499 pattern
template<typename T>
T declval();

template<typename Tp, typename Tag>
struct result_success {
    using type = Tp;
};

struct invoke_tag {};

// Simplified version of the pattern at line 2499
struct test_struct {
    template<typename Fp, typename Tp1, typename... Args>
    static result_success<decltype(
        (declval<Tp1>().*declval<Fp>())(declval<Args>()...)
    ), invoke_tag> test_func(int);
};

int main() {
    return 0;
}
