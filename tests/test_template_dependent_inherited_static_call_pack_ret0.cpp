struct InvokeFunctor {
	template <class Callable, class... Args>
	static constexpr auto call(Callable&& callable, Args&&... args)
		noexcept(noexcept(static_cast<Callable&&>(callable)(static_cast<Args&&>(args)...)))
		-> decltype(static_cast<Callable&&>(callable)(static_cast<Args&&>(args)...)) {
		return static_cast<Callable&&>(callable)(static_cast<Args&&>(args)...);
	}
};

template <class Callable, class First>
struct SelectInvoker : InvokeFunctor {};

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
	return invokeLike(function, value) == 42 ? 0 : 1;
}
