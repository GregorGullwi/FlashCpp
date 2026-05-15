template <typename...>
using void_t = void;

struct false_type {
	static constexpr bool value = false;
};

struct true_type {
	static constexpr bool value = true;
};

template <typename T, typename = void>
struct has_value_type : false_type {};

template <typename T>
struct has_value_type<T, void_t<typename T::value_type>> : true_type {};

struct WithValueType {
	using value_type = int;
};

struct WithoutValueType {
	using type = int;
};

int main() {
	if (!has_value_type<WithValueType>::value) {
		return 1;
	}
	if (has_value_type<WithoutValueType>::value) {
		return 2;
	}
	return 0;
}
