template <typename...>
using void_t = void;

struct false_type {
	static constexpr bool value = false;
};

struct true_type {
	static constexpr bool value = true;
};

template <typename T, typename = void>
struct has_nested_value_type : false_type {};

template <typename T>
struct has_nested_value_type<T, void_t<typename T::traits::value_type>> : true_type {};

struct Traits {
	using value_type = int;
};

struct WithNestedValueType {
	using traits = Traits;
};

struct WithoutNestedValueType {
	using traits = int;
};

int main() {
	if (!has_nested_value_type<WithNestedValueType>::value) {
		return 1;
	}
	if (has_nested_value_type<WithoutNestedValueType>::value) {
		return 2;
	}
	return 0;
}
