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

struct ReadValue {
	constexpr int operator()(int& value) const {
		return value;
	}
};

int main() {
	ReadValue function;
	int value = 42;
	return invokeLike(function, value) == 42
		? 0
		: 1;
}
