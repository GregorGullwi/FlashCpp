// Test: void constexpr lambdas with by-reference captures that mutate outer locals.
// C++20 permits constexpr lambdas that have no return statement (or an explicit
// "return;" statement) — the lambda return type is deduced as void.  Previously
// FlashCpp's constexpr evaluator treated "did not return a value" as a hard error
// even for void lambdas, so the by-reference capture writebacks never occurred.

// Case 1: void lambda with no args, one by-ref capture
constexpr int f1() {
	int x = 10;
	auto inc = [&x]() { x += 5; };
	inc();
	return x;  // 15
}
static_assert(f1() == 15);

// Case 2: void lambda with a parameter and one by-ref capture
constexpr int f2() {
	int sum = 0;
	auto add = [&sum](int v) { sum += v; };
	add(3);
	add(7);
	return sum;  // 10
}
static_assert(f2() == 10);

// Case 3: void lambda with two by-ref captures (swap)
constexpr int f3() {
	int x = 10;
	int y = 20;
	auto swap_xy = [&x, &y]() {
		int tmp = x;
		x = y;
		y = tmp;
	};
	swap_xy();
	return x - y;  // 20 - 10 = 10
}
static_assert(f3() == 10);

// Case 4: explicit "-> void" trailing return type
constexpr int f4() {
	int x = 0;
	auto reset = [&x]() -> void { x = 42; };
	reset();
	return x;
}
static_assert(f4() == 42);

// Case 5: void lambda with early "return;" path
constexpr int f5() {
	int x = 0;
	auto maybe_set = [&x](bool do_it) {
		if (!do_it) return;
		x = 99;
	};
	maybe_set(false);
	maybe_set(true);
	return x;  // 99
}
static_assert(f5() == 99);

// Case 6: range-based for with void lambda accumulator
constexpr int f6() {
	int sum = 0;
	int arr[] = {1, 2, 3, 4, 5};
	auto acc = [&sum](int v) { sum += v; };
	for (int v : arr) acc(v);
	return sum;  // 15
}
static_assert(f6() == 15);

// Case 7: two by-ref captures, both modified
constexpr int f7() {
	int a = 0;
	int b = 0;
	auto fill = [&a, &b](int va, int vb) {
		a = va;
		b = vb;
	};
	fill(3, 7);
	return a + b;  // 10
}
static_assert(f7() == 10);

int main() { return 0; }
