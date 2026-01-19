// Test anonymous typename parameter with complex default
template<typename T> struct remove_cv { using type = T; };
template<typename T> struct enable_if { using type = T; };
template<typename T, typename U> struct is_same { static constexpr bool value = false; };
template<typename T> struct is_same<T, T> { static constexpr bool value = true; };

template<typename _Tp,
         typename _Up = typename remove_cv<_Tp>::type,
         typename = typename enable_if<is_same<_Tp, _Up>::value>::type>
struct Test {
    _Tp t;
};

int main() {
    return 0;
}
