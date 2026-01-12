// Simpler test case
template<bool B>
struct bool_constant {
    static constexpr bool value = B;
};

template<typename T>
constexpr bool test_func() {
    return true;
}

// This pattern: test_func<T>() as template argument
template<typename T>
struct wrapper : bool_constant<test_func<T>()>
{ };

int main() {
    return wrapper<int>::value ? 0 : 1;
}
