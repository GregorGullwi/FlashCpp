// Test: overload resolution with integer-width variants using suffixed literals.
// Verifies that literal suffixes (L, UL, LL, U) correctly determine the
// argument type for overload selection, per C++20 [lex.icon] and [over.match].
//
// Previously, all suffixed integer literals were typed as Type::Int regardless
// of the L/LL suffix, causing the wrong overload to be selected.

int f(int x) { return x; }
int f(long x) { return (int)x + 100; }

int g(unsigned int x) { return (int)x; }
int g(unsigned long x) { return (int)x + 200; }

int h(long long x) { return (int)x + 300; }
int h(int x) { return x; }

int i(unsigned long long x) { return (int)x + 400; }
int i(int x) { return x; }

int main() {
	// 0L should call f(long)
	int r1 = f(0L);
	if (r1 != 100) return 1;

	// 0 should call f(int)
	int r2 = f(0);
	if (r2 != 0) return 2;

	// 42L should call f(long)
	int r3 = f(42L);
	if (r3 != 142) return 3;

	// 0UL should call g(unsigned long)
	int r4 = g(0UL);
	if (r4 != 200) return 4;

	// 0U should call g(unsigned int)
	int r5 = g(0U);
	if (r5 != 0) return 5;

	// 0LL should call h(long long)
	int r6 = h(0LL);
	if (r6 != 300) return 6;

	// 0 should call h(int)
	int r7 = h(0);
	if (r7 != 0) return 7;

	// 0ULL should call i(unsigned long long)
	int r8 = i(0ULL);
	if (r8 != 400) return 8;

	// 0 should call i(int)
	int r9 = i(0);
	if (r9 != 0) return 9;

	return 0;
}
