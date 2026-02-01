// Test dependent template instantiation placeholder names with underscore parameters.

template<typename _Tp>
struct is_void {
    static constexpr bool value = false;
};

template<>
struct is_void<void> {
    static constexpr bool value = true;
};

template<typename _Tp>
struct is_reference {
    static constexpr bool value = false;
};

template<typename _Tp>
struct is_reference<_Tp&> {
    static constexpr bool value = true;
};

template<typename...>
struct __or_;

template<typename B1>
struct __or_<B1> {
    static constexpr bool value = B1::value;
};

template<typename B1, typename B2>
struct __or_<B1, B2> {
    static constexpr bool value = B1::value || B2::value;
};

template<typename B>
struct __not_ {
    static constexpr bool value = !B::value;
};

template<typename _Tp>
struct is_nonref_void {
    using type = __not_<__or_<is_reference<_Tp>, is_void<_Tp>>>;
};

int main() {
    is_nonref_void<int> check;
    (void)check;
    return 0;
}
