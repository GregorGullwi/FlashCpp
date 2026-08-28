// Test that 3-candidate overload resolution detects ambiguity when a later
// "strictly better" candidate replaces the current best but an earlier
// incomparable candidate was discarded without re-evaluation.
//
// Candidate evaluation order in resolve_overload (single-pass):
//   A: f(double, double, short) -> ranks [Conversion, Conversion, Exact]
//   B: f(short, double, double) -> ranks [Exact, Conversion, Conversion]
//   C: f(int, int, short)       -> ranks [Promotion, Promotion, Exact]
//
// Step 1: A is first candidate -> becomes best.
// Step 2: B vs A: arg0 better, arg2 worse -> incomparable -> added to tied.
// Step 3: C vs A: arg0 better, arg1 better, arg2 equal -> strictly better
//         -> C replaces A, tied_candidates reset to [C], B is discarded.
//
// But C vs B: arg0 Promotion > Exact (worse), arg1 Promotion < Conversion
// (better), arg2 Exact < Conversion (better) -> incomparable.
// Neither C nor B dominates the other, so the correct result is AMBIGUOUS.
//
// BUG: resolve_overload's single-pass algorithm discards B when C replaces A
// without re-comparing B against C, so it incorrectly selects C as unique best.

int f(double a, double b, short c) { return 1; }
int f(short a, double b, double c) { return 2; }
int f(int a, int b, short c) { return 3; }

int main() {
	short x = 1;
	short y = 2;
	short z = 3;
	return f(x, y, z); // ambiguous: should fail to compile
}
