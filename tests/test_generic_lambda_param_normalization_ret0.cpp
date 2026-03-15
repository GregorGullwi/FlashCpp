int main() {
	int value = 3;
	auto mutate = [](auto&& x) -> decltype(auto) {
		x += 4;
		return (x);
	};

	mutate(value);
	return value == 7 ? 0 : 1;
}
