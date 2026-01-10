// Even simpler test - just decltype in template arguments for a static function return type
template<typename T>
T declval();

template<typename Tp>
struct result_success {
    using type = Tp;
};

struct test_struct {
    template<typename Tp1>
    static result_success<decltype(declval<Tp1>())> test_func(int);
};

int main() {
    return 0;
}
