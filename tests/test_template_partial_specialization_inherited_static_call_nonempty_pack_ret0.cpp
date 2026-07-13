// Regression: a non-empty function parameter pack must remain materialized
// while reparsing a dependent inherited static call in a trailing return type.

template <class Callable, class First>
struct InvokeBase {
	template <class C, class T, class... Rest>
	static constexpr auto call(C&& callable, T&& first, Rest&&... rest)
		noexcept(noexcept(static_cast<C&&>(callable)(static_cast<T&&>(first), static_cast<Rest&&>(rest)...)))
		-> decltype(static_cast<C&&>(callable)(static_cast<T&&>(first), static_cast<Rest&&>(rest)...)) {
		return static_cast<C&&>(callable)(static_cast<T&&>(first), static_cast<Rest&&>(rest)...);
	}
};

template <class Callable, class First, bool IsCallable = true>
struct SelectInvoker;

template <class Callable, class First>
struct SelectInvoker<Callable, First, true> : InvokeBase<Callable, First> {};

template <class Callable, class First, class... Rest>
constexpr auto invokeLike(Callable&& callable, First&& first, Rest&&... rest)
	noexcept(noexcept(SelectInvoker<Callable, First>::call(
		static_cast<Callable&&>(callable),
		static_cast<First&&>(first),
		static_cast<Rest&&>(rest)...)))
	-> decltype(SelectInvoker<Callable, First>::call(
		static_cast<Callable&&>(callable),
		static_cast<First&&>(first),
		static_cast<Rest&&>(rest)...)) {
	return SelectInvoker<Callable, First>::call(
		static_cast<Callable&&>(callable),
		static_cast<First&&>(first),
		static_cast<Rest&&>(rest)...);
}

struct AddValues {
	constexpr int operator()(int& first, short second) const {
		return first + second;
	}
};

int main() {
	AddValues function;
	int first = 40;
	short second = 2;
	using Result = decltype(invokeLike(function, first, second));
	return sizeof(Result) == sizeof(int) ? 0 : 1;
}
