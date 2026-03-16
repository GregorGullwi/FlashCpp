int main() {
	auto invoke = [](auto callable, int value) {
		return callable(value);
	};

	auto plus_one = [](int value) {
		return value + 1;
	};

	return invoke(plus_one, 41) == 42 ? 0 : 1;
}
