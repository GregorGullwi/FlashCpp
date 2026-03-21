// Test: qualification adjustment should rank as ExactMatch, not Conversion.
//
// Two overloads:
//   int process(const int* p, int x);    // (1) — returns 1
//   int process(int* p, double x);       // (2) — returns 2
//
// Calling process(ptr, 42) where ptr is int*:
//
// Per C++20 [over.ics.rank]/3.2.5:
//   Candidate (1): arg1 int*->const int* = qualification adjustment (ExactMatch category)
//                  arg2 int->int = identity (ExactMatch)
//   Candidate (2): arg1 int*->int* = identity (ExactMatch)
//                  arg2 int->double = FloatingIntegralConversion (Conversion)
//
// Candidate (1) should win: (ExactMatch, ExactMatch) beats (ExactMatch, Conversion).
// Correct return value: 1.

int process(const int* p, int x) {
	return 1;
}

int process(int* p, double x) {
	return 2;
}

int main() {
	int val = 42;
	int* ptr = &val;
	return process(ptr, 42);  // Should select overload (1) -> returns 1
}
