int target() {
	return 42;
}

template <auto Function>
struct selected_function {
	static constexpr int value = 1;
};

template <>
struct selected_function<&target> {
	static constexpr int value = 42;
};

template <typename T>
struct deferred_function_base : selected_function<(sizeof(T) > 0 ? &target : &target)> {};

int main() {
	return deferred_function_base<int>::value == 42 ? 0 : 1;
}
