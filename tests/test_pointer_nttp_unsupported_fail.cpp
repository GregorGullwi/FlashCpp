int global_value = 0;

template <int* P>
struct pointer_tag {
	static constexpr int value = 1;
};

template <>
struct pointer_tag<&global_value> {
	static constexpr int value = 42;
};

int main() {
	return pointer_tag<&global_value>::value;
}
