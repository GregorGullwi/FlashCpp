// Test alias template followed by ::type member access
// This tests the pattern from type_traits line 2583:
// using type = typename conditional_t<...>::type;

template<bool V>
struct bool_constant {
    static constexpr bool value = V;
};

using true_type = bool_constant<true>;
using false_type = bool_constant<false>;

template<typename T, typename U>
struct is_same : false_type {};

template<typename T>
struct is_same<T, T> : true_type {};

template<typename Base, typename Derived>
struct is_base_of : false_type {};

// or_ template
template<typename...>
struct or_;

template<>
struct or_<> : false_type {};

template<typename B1>
struct or_<B1> : B1 {};

template<typename B1, typename B2>
struct or_<B1, B2> : bool_constant<B1::value || B2::value> {};

// conditional with nested member template type
template<bool Cond>
struct conditional {
    template<typename If, typename Else>
    using type = If;
};

template<>
struct conditional<false> {
    template<typename If, typename Else>
    using type = Else;
};

// Alias template that resolves to conditional<_Cond>::template type<_If, _Else>
template<bool _Cond, typename _If, typename _Else>
using conditional_t = typename conditional<_Cond>::template type<_If, _Else>;

// Result types with ::type member
template<typename A, typename B>
struct result_ref {
    using type = int;
};

template<typename A, typename B>
struct result_deref {
    using type = double;
};

// The pattern from type_traits:
// using type = typename conditional_t<...>::type;
template<typename Class, typename Arg>
struct result_of {
    using Argval = Arg;
    // Access ::type on alias template result
    using type = typename conditional_t<or_<is_same<Argval, Class>,
        is_base_of<Class, Argval>>::value,
        result_ref<int, Arg>,
        result_deref<int, Arg>
    >::type;
};

int main() {
    result_of<int, int>::type x = 42;  // Should resolve to int
    return x;  // Returns 42
}
