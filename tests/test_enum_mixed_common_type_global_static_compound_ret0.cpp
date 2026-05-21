enum Flags : unsigned char {
	FlagA = 1,
	FlagB = 2,
	FlagC = 4,
};

int globalAccum = 1;

int main() {
	Flags rhs = FlagB;

	// Mixed enum/int binary operators in both operand orders.
	if ((rhs | 1) != 3)
		return 1;
	if ((1 | rhs) != 3)
		return 2;
	if ((rhs & 3) != 2)
		return 3;
	if ((3 & rhs) != 2)
		return 4;
	if ((rhs == 2) == 0)
		return 5;
	if ((2 == rhs) == 0)
		return 6;

	// Global/static compound assignment paths with enum RHS.
	globalAccum += rhs;
	if (globalAccum != 3)
		return 7;

	static int localStaticAccum = 1;
	localStaticAccum += rhs;
	if (localStaticAccum != 3)
		return 8;

	int localAccum = 1;
	localAccum += rhs;
	if (localAccum != 3)
		return 9;

	return 0;
}
