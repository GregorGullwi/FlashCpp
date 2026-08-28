int global_value = 0;

template <int& R>
struct reference_tag {
	static constexpr int value = 42;
};

int main() {
	return reference_tag<global_value>::value;
}
