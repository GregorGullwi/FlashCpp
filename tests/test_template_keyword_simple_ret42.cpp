// Simpler test for ::template keyword

template<bool B>
struct Conditional {
    template<typename T, typename U>
    struct type_helper {
        using type = T;
    };
};

template<>
struct Conditional<false> {
    template<typename T, typename U>
    struct type_helper {
        using type = U;
    };
};

// Using ::template to access dependent member template
template<bool B, typename T, typename U>
using conditional_t = typename Conditional<B>::template type_helper<T, U>::type;

// Simple test struct
struct A { static constexpr int value = 42; };
struct B { static constexpr int value = 100; };

int main() {
    // conditional_t<true, A, B> should select A, so value = 42
    return conditional_t<true, A, B>::value;
}
