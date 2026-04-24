// Regression test for nested wrapped pack deduction.
// The Phase 6 wrapped-pack logic handled Box<Ts>..., but nested wrappers like
// Wrap<Box<Ts>>... still left Ts empty and folded over an empty pack.

template<typename T>
struct Box {
	T value;
};

template<typename T>
struct Wrap {
	T value;
};

template<typename... Ts>
int sum_nested_boxes(Wrap<Box<Ts>>... xs) {
	return (0 + ... + static_cast<int>(xs.value.value));
}

int main() {
	Wrap<Box<int>> a{{1}};
	Wrap<Box<double>> b{{2.0}};
	Wrap<Box<char>> c{{3}};

	if (sum_nested_boxes(a, b) != 3) return 1;
	if (sum_nested_boxes(a, b, c) != 6) return 2;
	return 0;
}
