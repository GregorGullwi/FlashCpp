// Variadic __is_nothrow_constructible(T, Args...) must expand Args in a
// noexcept-specifier after substitution. This is the MSVC
// is_nothrow_constructible_v<_Ty, _Args...> pattern used by pair/swap.

template <class Type, class... Args>
constexpr bool is_nothrow_constructible_v = __is_nothrow_constructible(Type, Args...);

template <class Type>
void takeValue(Type value) noexcept(is_nothrow_constructible_v<Type, const Type&>) {
	(void)value;
}

template <class Type, class... Args>
struct EmplaceHolder {
	Type value;

	template <class... CtorArgs>
	constexpr explicit EmplaceHolder(CtorArgs&&... args)
		noexcept(is_nothrow_constructible_v<Type, CtorArgs...>);
};

struct Tiny {
	char byte;
};

struct Wide {
	int a;
	int b;
};

int main() {
	Tiny tiny{'A'};
	takeValue(7);
	takeValue(tiny);
	const bool int_nothrow = noexcept(takeValue(7));
	const bool tiny_nothrow = noexcept(takeValue(tiny));
	return (int_nothrow && tiny_nothrow &&
			sizeof(EmplaceHolder<char>) != 0 &&
			sizeof(EmplaceHolder<Tiny>) != 0 &&
			sizeof(EmplaceHolder<Wide>) != 0)
		? 0
		: 1;
}
