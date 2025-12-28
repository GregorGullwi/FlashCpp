// Validates that template-dependent base classes using member aliases
// (e.g., helper<T>::type) are resolved during instantiation.

struct false_type {
	static constexpr bool value = false;
};

struct true_type {
	static constexpr bool value = true;
};

template<typename T>
struct helper {
	using type = false_type;
};

template<>
struct helper<int> {
	using type = true_type;
};

template<typename T>
struct is_int : helper<T>::type {
};

int main() {
	static_assert(is_int<int>::value);
	static_assert(!is_int<float>::value);
	return is_int<int>::value ? 0 : 1;
}
