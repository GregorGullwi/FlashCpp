// Regression test: constexpr comparisons used in if-statements.
// Ensures constexpr call/comparison paths are evaluable in regular if conditions.

constexpr int add(int a, int b) {
	return a + b;
}

constexpr auto make_mul3() {
	return [](int x) { return x * 3; };
}

constexpr auto mul3 = make_mul3();
constexpr int g_x = 7;

int main() {
	if (add(3, 4) != 7)
		return 1;
	if (g_x != 7)
		return 2;
	if (mul3(4) != 12)
		return 3;
	if (mul3(5) != 15)
		return 4;
	return 0;
}
