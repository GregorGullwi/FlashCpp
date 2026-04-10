namespace std {
	template<bool Cond, typename Type = void>
	struct enable_if { };

	template<typename Type>
	struct enable_if<true, Type> {
		using type = Type;
	};

	template<bool Value>
	struct bool_constant {
		static constexpr bool value = Value;
	};

	using true_type = bool_constant<true>;
	using false_type = bool_constant<false>;

	template<typename Type>
	struct is_move_constructible : true_type { };

	template<typename Type>
	struct is_move_assignable : true_type { };

	template<typename Type>
	struct __is_tuple_like : false_type { };

	template<typename Trait>
	struct __not_ : bool_constant<!Trait::value> { };

	template<typename...>
	struct __and_;

	template<>
	struct __and_<> : true_type { };

	template<typename First>
	struct __and_<First> : First { };

	template<typename First, typename... Rest>
	struct __and_<First, Rest...> : enable_if<First::value, __and_<Rest...>>::type { };

	template<typename Type>
	Type&& declval() noexcept;

	template<typename Type>
	typename enable_if<__and_<__not_<__is_tuple_like<Type>>, is_move_constructible<Type>, is_move_assignable<Type>>::value>::type
	swap(Type&, Type&) noexcept;

	namespace __swappable_details {
		using std::swap;

		struct __do_is_swappable_impl {
			template<typename Type, typename = decltype(swap(std::declval<Type&>(), std::declval<Type&>()))>
			static true_type __test(int);

			template<typename>
			static false_type __test(...);
		};
	}
}

using swappable_int = decltype(std::__swappable_details::__do_is_swappable_impl::__test<int>(0));

int main() {
	static_assert(swappable_int::value);
	return 0;
}
