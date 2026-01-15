// Test: noexcept(expr) as template argument - returns 0 if successful
// This pattern is used in <type_traits> for detecting noexcept destructors

template<bool B>
struct bool_constant {
    static constexpr bool value = B;
};

using false_type = bool_constant<false>;
using true_type = bool_constant<true>;

template<typename T>
T& declval();

struct test_struct {
    template<typename T>
    // noexcept(declval<T&>().~T()) - destructor call inside noexcept
    static bool_constant<noexcept(declval<T&>().~T())> check(int);
    
    template<typename>
    static false_type check(...);
};

int main() {
    // If this compiles, the noexcept template argument parsing works
    return 0;
}
