// A dependent void_t probe must not report overload failure before instantiation.
// C++20 [temp.res] defers overload resolution for the dependent swap call.

template <class T>
struct remove_reference {
	using type = T;
};

template <class T>
struct remove_reference<T&> {
	using type = T;
};

template <class T>
struct remove_reference<T&&> {
	using type = T;
};

template <class T>
using remove_reference_t = typename remove_reference<T>::type;

template <class T>
remove_reference_t<T>&& declval() noexcept;

template <bool B>
struct bool_constant {
	static constexpr bool value = B;
};

using true_type = bool_constant<true>;
using false_type = bool_constant<false>;

template <class...>
using void_t = void;

template <bool B, class T = void>
struct enable_if {};

template <class T>
struct enable_if<true, T> {
	using type = T;
};

template <bool B, class T = void>
using enable_if_t = typename enable_if<B, T>::type;

template <class T, enable_if_t<true, int> = 0>
void swap(T&, T&) noexcept;

template <class T, unsigned long Size, enable_if_t<true, int> = 0>
void swap(T (&)[Size], T (&)[Size]) noexcept;

template <class T1, class T2, class = void>
struct SwappableHelper : false_type {};

template <class T1, class T2>
struct SwappableHelper<T1, T2, void_t<decltype(swap(declval<T1>(), declval<T2>()))>> : true_type {};

int main() {
	return SwappableHelper<int&, int&>::value ? 0 : 1;
}
