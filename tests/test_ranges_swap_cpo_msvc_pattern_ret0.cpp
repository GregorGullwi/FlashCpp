// Closer reproduction of MSVC <concepts> ranges::_Swap::_Cpo
template <class T, class U>
constexpr bool is_same_v = false;
template <class T>
constexpr bool is_same_v<T, T> = true;

template <class T>
struct remove_reference { using type = T; };
template <class T>
struct remove_reference<T&> { using type = T; };
template <class T>
struct remove_reference<T&&> { using type = T; };
template <class T>
using remove_reference_t = typename remove_reference<T>::type;

template <class T>
concept class_or_enum =
	__is_class(remove_reference_t<T>) || __is_enum(remove_reference_t<T>) || __is_union(remove_reference_t<T>);

template <class T>
concept move_constructible = true;
template <class T, class U>
concept assignable_from = true;

namespace ranges {
namespace SwapDetail {
template <class T>
void swap(T&, T&) = delete;

template <class T1, class T2>
concept use_adl_swap =
	(class_or_enum<T1> || class_or_enum<T2>) && requires(T1&& t, T2&& u) {
		swap(static_cast<T1&&>(t), static_cast<T2&&>(u));
	};

struct Cpo {
	template <class T1, class T2>
		requires use_adl_swap<T1, T2>
	static constexpr void operator()(T1&& t, T2&& u) {
		swap(static_cast<T1&&>(t), static_cast<T2&&>(u));
	}

	template <class T>
		requires (!use_adl_swap<T&, T&> && move_constructible<T> && assignable_from<T&, T>)
	static constexpr void operator()(T& x, T& y) {
		T tmp(static_cast<T&&>(x));
		x = static_cast<T&&>(y);
		y = static_cast<T&&>(tmp);
	}

	template <class T1, class T2, unsigned long long Size>
	static constexpr void operator()(T1 (&t)[Size], T2 (&u)[Size])
		requires requires(Cpo fn) { fn(t[0], u[0]); }
	{
		for (unsigned long long i = 0; i < Size; ++i) {
			operator()(t[i], u[i]);
		}
	}
};
} // namespace SwapDetail

inline constexpr SwapDetail::Cpo swap{};
} // namespace ranges

int main() {
	int left = 1;
	int right = 2;
	ranges::swap(left, right);
	return left == 2 && right == 1 ? 0 : 1;
}
