int main() {
	int seed = 40;
	auto outer = [value = [seed]() {
		return seed + 2;
	}()]() {
		return value;
	};

	return outer() == 42 ? 0 : 1;
}