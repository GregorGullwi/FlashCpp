// Test: implicit signed→unsigned and int→long-long conversions in return statements (Phase 2).
// Validates both promotion (int→long long) and conversion (int→unsigned int).

unsigned int to_unsigned() { return 200; }

long long to_long_long() { return 42; }

unsigned long to_ulong_from_param(int p) { return p; }

int main() {
	unsigned int a = to_unsigned();
	long long b = to_long_long();
	unsigned long c = to_ulong_from_param(99);
	return (int)(a - 200u) + (int)(b - 42LL) + (int)(c - 99UL);
}
