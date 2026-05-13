int target() {
	return 42;
}

template <int (*F)()>
struct function_pointer_tag {
	static constexpr int value = 42;
};

int main() {
	return function_pointer_tag<&target>::value;
}
