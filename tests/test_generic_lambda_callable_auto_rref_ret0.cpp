int main() {
	auto plus_one = [](int value) {
		return value + 1;
	};

	auto invoke = [](auto&& callable, int value) {
		return callable(value);
	};

	return invoke(plus_one, 41) == 42 ? 0 : 1;
}
