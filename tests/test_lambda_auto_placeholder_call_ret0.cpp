int main() {
	auto outer = [](auto base) {
		int local = base + 2;
		auto lambda = [local]() {
			return local;
		};
		return lambda();
	};

	return outer(40) == 42 ? 0 : 1;
}
