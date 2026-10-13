int target() {
	return 42;
}

template <auto Function>
struct selected_function_value {
	static constexpr int value = 1;
};

template <>
struct selected_function_value<&target> {
	static constexpr int value = 42;
};

template <typename T>
struct deferred_value_reader {
	static constexpr int value =
		selected_function_value<(sizeof(T) > 0 ? &target : &target)>::value;
};

int main() {
	return deferred_value_reader<int>::value == 42 ? 0 : 1;
}
