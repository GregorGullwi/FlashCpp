// TODO: This test currently returns 1 (is_swappable = true) instead of 0 (false).
// The correct C++20 behavior is to return 0 because pair<const int, int> is NOT
// swappable (the matching swap overload is = delete). However, FlashCpp does not
// yet evaluate decltype(...) expressions in default template arguments during
// SFINAE-based overload resolution. Once that feature is implemented, this test
// should be renamed to _ret0.cpp to expect the correct return value of 0.

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

	// Forward-declare swap overloads so they are visible inside pair::swap
	template<typename Type>
	typename enable_if<!is_const<Type>::value, void>::type
	swap(Type& left, Type& right);

	template<typename Type>
	typename enable_if<is_const<Type>::value, void>::type
	swap(Type&, Type&) = delete;

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

	// Definitions for forward-declared swap overloads
	template<typename Type>
	typename enable_if<!is_const<Type>::value, void>::type
	swap(Type& left, Type& right) {
		Type temp = left;
		left = right;
		right = temp;
	}

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
