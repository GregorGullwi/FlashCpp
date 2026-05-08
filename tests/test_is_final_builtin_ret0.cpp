// Test: __is_final built-in type trait
// Final class should return true, regular class should return false.

class RegularClass {
	int x;
};

class FinalClass final {
	int y;
};

static_assert(__is_final(FinalClass), "__is_final(FinalClass) should be true");
static_assert(!__is_final(RegularClass), "!__is_final(RegularClass) should be true");

int main() {
	return 0;
}
