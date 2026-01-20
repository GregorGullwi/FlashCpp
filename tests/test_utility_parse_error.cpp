// Minimal test case for bits/utility.h parse error
// This should work in isolation

template<typename T> struct remove_cv { using type = T; };
template<typename T, T v> struct integral_constant { static constexpr T value = v; };
template<typename T, typename U> struct is_same : integral_constant<bool, false> {};
template<typename T> struct is_same<T, T> : integral_constant<bool, true> {};
template<bool, typename T = void> struct enable_if {};
template<typename T> struct enable_if<true, T> { using type = T; };

template<typename _Tp> struct tuple_size;

// This is the pattern that fails in context
template<typename _Tp,
         typename _Up = typename remove_cv<_Tp>::type,
         typename = typename enable_if<is_same<_Tp, _Up>::value>::type,
         size_t = tuple_size<_Tp>::value>
  using __enable_if_has_tuple_size = _Tp;

int main() {
    return 0;
}
