// Test: unified buildConversionPlan coverage (Phase 11).
// Exercises every StandardConversionKind category through initialization
// conversions, verifying that the unified conversion-plan helper classifies
// and applies each conversion correctly.
// Uses helper functions to avoid register-pressure issues in single main().

int test_integral_promotion() {
 // IntegralPromotion: short -> int (rank < int promoted to int)
	short s = 10;
	int promoted = s;
	int r1 = promoted - 10;	// expect 0

 // IntegralPromotion: char -> int
	char c = 'A';  // 65
	int ci = c;
	int r2 = ci - 65;  // expect 0

 // IntegralPromotion: bool -> int
	bool bt = true;
	int bi = bt;
	int r3 = bi - 1;	 // expect 0

	return r1 + r2 + r3;
}

int test_integral_conversion() {
 // IntegralConversion: int -> long long
	int i = 42;
	long long ll = i;
	int r1 = (int)(ll - 42LL);  // expect 0

 // IntegralConversion: long long -> int (narrowing, valid implicit)
	long long big = 100LL;
	int narrow = big;
	int r2 = narrow - 100;  // expect 0

 // IntegralConversion: unsigned int -> int
	unsigned int ui = 50u;
	int si = ui;
	int r3 = si - 50;  // expect 0

	return r1 + r2 + r3;
}

int test_floating_promotion() {
 // FloatingPromotion: float -> double
	float f = 2.5f;
	double d = f;
	return (int)(d - 2.5);  // expect 0
}

int test_floating_conversion() {
 // FloatingConversion: double -> float
	double dd = 3.5;
	float ff = dd;
	return (int)(ff - 3.5f);	 // expect 0
}

int test_floating_integral_conversion() {
 // FloatingIntegralConversion: int -> double
	int x = 7;
	double dx = x;
	int r1 = (int)(dx - 7.0);  // expect 0

 // FloatingIntegralConversion: double -> int (truncation)
	double dy = 9.8;
	int yi = dy;
	int r2 = yi - 9;	 // expect 0

	return r1 + r2;
}

int test_boolean_conversion() {
 // BooleanConversion: int -> bool
	int nonzero = 5;
	bool b1 = nonzero;
	int r1 = b1 ? 0 : 1;	 // expect 0

 // BooleanConversion: float -> bool
	float fz = 1.5f;
	bool b2 = fz;
	int r2 = b2 ? 0 : 1;	 // expect 0

	return r1 + r2;
}

enum PromE { PA = 1,
			 PB = 2,
			 PC = 3 };
enum IntE { IX = 10 };
enum BoolE { BZero = 0,
			 BOne = 1 };
enum FltE { FVal = 5 };

int test_enum_promotion() {
 // IntegralPromotion: enum -> int [conv.prom]/4
	PromE e = PB;
	int ei = e;
	return ei - 2;  // expect 0
}

int test_enum_to_integral() {
 // IntegralConversion: enum -> long long
	IntE e = IX;
	long long ll = e;
	return (int)(ll - 10LL);	 // expect 0
}

int test_enum_to_bool() {
 // BooleanConversion: enum -> bool [conv.bool]
	BoolE e1 = BOne;
	bool b1 = e1;
	int r1 = b1 ? 0 : 1;	 // expect 0

	BoolE e0 = BZero;
	bool b0 = e0;
	int r2 = b0 ? 1 : 0;	 // expect 0

	return r1 + r2;
}

int test_enum_to_floating() {
 // FloatingIntegralConversion: enum -> double
	FltE e = FVal;
	double d = e;
	return (int)(d - 5.0);  // expect 0
}

int main() {
	return test_integral_promotion() + test_integral_conversion() + test_floating_promotion() + test_floating_conversion() + test_floating_integral_conversion() + test_boolean_conversion() + test_enum_promotion() + test_enum_to_integral() + test_enum_to_bool() + test_enum_to_floating();
}
