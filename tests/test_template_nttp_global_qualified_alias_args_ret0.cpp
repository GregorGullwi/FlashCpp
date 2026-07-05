namespace meta {
	template <bool Value>
	struct bool_constant {
		static constexpr bool value = Value;
	};

	template <class...>
	struct conjunction : bool_constant<true> {};

	template <class First>
	struct conjunction<First> : First {};

	template <class First, class... Rest>
	struct conjunction<First, Rest...> : bool_constant<First::value && conjunction<Rest...>::value> {};

	template <class... Types>
	constexpr bool conjunction_v = conjunction<Types...>::value;

	template <class Type, class Alloc>
	struct uses_allocator : bool_constant<true> {};

	template <class Type, class... Args>
	struct is_constructible : bool_constant<true> {};

	template <bool Condition, class Type>
	struct enable_if {};

	template <class Type>
	struct enable_if<true, Type> {
		using type = Type;
	};

	template <bool Condition, class Type>
	using enable_if_t = typename enable_if<Condition, Type>::type;

	struct allocator_arg_t {};
}

template <class Type>
struct holder {
	template <class Alloc, class... Other,
		::meta::enable_if_t<::meta::conjunction_v<::meta:: uses_allocator<Type, Alloc>,
								::meta:: is_constructible<Type, ::meta::allocator_arg_t, const Alloc&, Other...>>,
			int> = 0>
	int construct(const Alloc&, ::meta::allocator_arg_t, Other&&...) {
		return 42;
	}
};

int main() {
	return 0;
}
