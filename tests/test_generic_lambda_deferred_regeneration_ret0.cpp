int main() {
	auto inner = [](auto value) {
		return value + 1;
	};

	auto make_outer = [inner]() {
		return [inner]() {
			return inner(5);
		};
	};

	auto outer = make_outer();
	return outer() == 6 ? 0 : 1;
}
