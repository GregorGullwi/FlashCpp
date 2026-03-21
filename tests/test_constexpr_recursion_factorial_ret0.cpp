// Test: constexpr recursion - factorial and mutual recursion
// factorial(n) = n * (n-1) * ... * 1

constexpr long long factorial(int n) {
	return n <= 1 ? 1LL : (long long)n * factorial(n - 1);
}

// Power function using recursion
constexpr long long power(long long base, int exp) {
	if (exp == 0) return 1LL;
	return base * power(base, exp - 1);
}

static_assert(factorial(0) == 1);
static_assert(factorial(1) == 1);
static_assert(factorial(5) == 120);
static_assert(factorial(10) == 3628800);

static_assert(power(2, 0) == 1);
static_assert(power(2, 8) == 256);
static_assert(power(3, 4) == 81);
static_assert(power(10, 6) == 1000000);

int main() {
	constexpr long long f5 = factorial(5);
	constexpr long long p8 = power(2, 8);
	static_assert(f5 == 120);
	static_assert(p8 == 256);
	return 0;
}
