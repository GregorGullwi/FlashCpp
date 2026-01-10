// Test decltype with .* in template arguments
template<typename T>
T declval();

template<typename Tp>
struct result_success {
    using type = Tp;
};

struct test_struct {
    template<typename Fp, typename Tp1>
    static result_success<decltype(declval<Tp1>().*declval<Fp>())> test_func(int);
};

int main() {
    return 0;
}
