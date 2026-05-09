struct Pair {
	int a;
	int b;
};

constexpr Pair global_pair{10, 32};

int main() {
	constexpr Pair local_pair{7, 5};

	constexpr int global_sum = global_pair.a + global_pair.b;
	constexpr int local_expr = (local_pair.a * 4) + (local_pair.b * 2);
	constexpr int mixed_expr = (global_pair.a - local_pair.b) + (global_pair.b / 8);

	if (global_sum != 42) {
		return 1;
	}
	if (local_expr != 38) {
		return 2;
	}
	if (mixed_expr != 9) {
		return 3;
	}
	return 42;
}
