// Test if the issue is the template argument or function return type parsing
template<typename T>
T declval();

template<typename Tp>
struct result_success {
    using type = Tp;
};

struct test_struct {
    template<typename Fp, typename Tp1, typename... Args>
    static decltype((declval<Tp1>().*declval<Fp>())(declval<Args>()...)) test_func2(int);
};

int main() {
    return 0;
}
