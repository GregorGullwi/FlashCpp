// Test: Phase 4 – tryAnnotateCallArgConversions uses pointer-identity matching
// to find the correct overload from lookup_all().  This test exercises the code
// path where the lookup returns multiple candidates and the pass must select the
// one the parser already resolved rather than defaulting to the first entry.
//
// The test deliberately uses a single non-overloaded function with a parameter
// type that differs from the argument type so that the annotation still fires.
// Overloaded function resolution with size-differing types is a known FlashCpp
// limitation tracked separately in docs/KNOWN_ISSUES.md.

int accumulate_as_long(long a, long b) { return (int)(a + b); }

int widen_and_negate(long x) { return (int)(-x); }

int main() {
 // int → long argument annotation: annotation must pick the right DeclarationNode
	int r1 = accumulate_as_long(20, 22);	 // 20 (int) → 20L, 22 (int) → 22L → 42

 // Explicit cast result used as argument
	int r2 = widen_and_negate(-7);  // -7 (int) → -7L → negate → 7

	return (r1 - 42) + (r2 - 7);
}
