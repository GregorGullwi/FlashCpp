// Test template parameter with typename in default

template<typename T> struct remove_cv { using type = T; };
template<typename T> struct enable_if { using type = T; };
template<typename T, typename U> struct is_same { static constexpr bool value = false; };
template<typename T> struct is_same<T, T> { static constexpr bool value = true; };
template<typename T> struct tuple_size;
template<typename T> struct tuple_size<T> { static constexpr size_t value = 0; };

template<typename _Tp,
	   typename _Up = typename remove_cv<_Tp>::type,
	   typename = typename enable_if<is_same<_Tp, _Up>::value>::type,
	   size_t = tuple_size<_Tp>::value>
struct Test {
    _Tp t;
};

int main() {
    return 0;
}
