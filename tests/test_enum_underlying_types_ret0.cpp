// Test: enum with non-int underlying types.
// Verifies that enums with explicit underlying types (short, long long, etc.)
// are correctly lowered to their underlying integer representation in IR.

enum SmallEnum : short { A = 1,
						 B = 2,
						 C = 3 };
enum BigEnum : long long { X = 100000000LL,
						   Y = 200000000LL };
enum UnsignedEnum : unsigned int { U1 = 0,
								   U2 = 4000000000U };

int main() {
 // short underlying type
	short s = static_cast<short>(A + B);
	if (s != 3)
		return 1;

 // long long underlying type
	long long ll = static_cast<long long>(X) + static_cast<long long>(Y);
	if (ll != 300000000LL)
		return 2;

 // unsigned int underlying type
	unsigned int u = static_cast<unsigned int>(U2);
	if (u != 4000000000U)
		return 3;

	return 0;
}
