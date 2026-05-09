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
	constexpr int bitwise_expr = (global_pair.a << 2) | (local_pair.b & 0x3);
	constexpr int compare_expr = (global_pair.b > local_pair.a) ? 1 : 0;
	constexpr int logical_expr = (global_pair.a == 10 && local_pair.b == 5) ? 1 : 0;

	if (global_sum != 42) {
		return 1;
	}
	if (local_expr != 38) {
		return 2;
	}
	if (mixed_expr != 9) {
		return 3;
	}
	if (bitwise_expr != 41) {
		return 4;
	}
	if (compare_expr != 1) {
		return 5;
	}
	if (logical_expr != 1) {
		return 6;
	}
	return 42;
}
