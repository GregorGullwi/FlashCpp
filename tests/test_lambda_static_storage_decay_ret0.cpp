auto global_add_one = [](int x) {
	return x + 1;
};

int main() {
	auto global_fp = +global_add_one;

	static auto static_add_two = [](int x) {
		return x + 2;
	};
	auto static_fp = +static_add_two;

	return (global_fp(41) == 42 && static_fp(40) == 42) ? 0 : 1;
}