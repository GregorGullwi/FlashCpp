// Test: complex pack expansion - calling a function on each pack element
// This tests the "TODO Complex expression" path

int do_abs(int x) { return x < 0 ? -x : x; }
int add3(int a, int b, int c) { return a + b + c; }

template <typename... Args>
int sum_abs(Args... args) {
	return add3(do_abs(args)...);
}

int main() {
	return sum_abs(-10, 15, -17);  // abs(-10)+15+abs(-17) = 10+15+17 = 42
}
