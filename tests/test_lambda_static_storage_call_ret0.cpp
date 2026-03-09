auto global_add = [](int x, int y) {
	return x + y;
};

int main() {
	static auto static_twice = [](int x) {
		return x * 2;
	};

	return (global_add(19, 23) == 42 && static_twice(21) == 42) ? 0 : 1;
}