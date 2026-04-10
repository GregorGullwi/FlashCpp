namespace stdlike {
	template<bool Condition, typename Type = void>
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
	struct is_const : false_type { };

	template<typename Type>
	struct is_const<const Type> : true_type { };

	template<typename Type>
	Type&& declval() noexcept;

	template<typename First, typename Second>
	struct pair {
		First first;
		Second second;

		void swap(pair& other) {
			using stdlike::swap;
			swap(first, other.first);
			swap(second, other.second);
		}
	};

	template<typename Type>
	typename enable_if<!is_const<Type>::value, void>::type
	swap(Type& left, Type& right) {
		Type temp = left;
		left = right;
		right = temp;
	}

	template<typename Type>
	typename enable_if<is_const<Type>::value, void>::type
	swap(Type&, Type&) = delete;

	template<typename First, typename Second>
	typename enable_if<!is_const<First>::value && !is_const<Second>::value, void>::type
	swap(pair<First, Second>& left, pair<First, Second>& right) {
		left.swap(right);
	}

	template<typename First, typename Second>
	typename enable_if<is_const<First>::value || is_const<Second>::value, void>::type
	swap(pair<First, Second>&, pair<First, Second>&) = delete;

	namespace detail {
		template<typename Type, typename = decltype(swap(declval<Type&>(), declval<Type&>()))>
		true_type test(int);

		template<typename>
		false_type test(...);
	}

	template<typename Type>
	struct is_swappable : decltype(detail::test<Type>(0)) { };
}

int main() {
	return stdlike::is_swappable<stdlike::pair<const int, int>>::value ? 1 : 0;
}
