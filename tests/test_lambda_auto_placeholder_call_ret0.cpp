int main() {
	auto outer = [](auto base) {
		auto lambda = [base]() {
			return base + 2;
		};
		return lambda();
	};

	return outer(40) == 42 ? 0 : 1;
}
