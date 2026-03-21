// Test: constexpr recursion - Fibonacci sequence
// fib(0)=0, fib(1)=1, fib(2)=1, fib(3)=2, fib(5)=5, fib(10)=55, fib(15)=610

// if/return style
constexpr int fib(int n) {
	if (n <= 1) return n;
	return fib(n - 1) + fib(n - 2);
}

// ternary style
constexpr long long fib_t(long long n) {
	return n <= 1 ? n : fib_t(n - 1) + fib_t(n - 2);
}

static_assert(fib(0) == 0);
static_assert(fib(1) == 1);
static_assert(fib(2) == 1);
static_assert(fib(3) == 2);
static_assert(fib(5) == 5);
static_assert(fib(10) == 55);
static_assert(fib(15) == 610);

static_assert(fib_t(0) == 0LL);
static_assert(fib_t(1) == 1LL);
static_assert(fib_t(10) == 55LL);

int main() {
	constexpr int r = fib(10);
	static_assert(r == 55);
	return 0;
}
