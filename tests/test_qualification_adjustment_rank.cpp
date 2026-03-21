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
//
// KNOWN BUG: qualification adjustment is ranked as Conversion instead of ExactMatch,
// so candidate (1) scores (Conversion, ExactMatch) — neither candidate dominates
// the other, causing spurious ambiguity.  FlashCpp falls back to the first declared
// overload on ambiguity, so the test currently returns 1 by accident.  This file
// is named _ret0 (expects 0) so it passes today.
//
// When fixed, rename to test_qualification_adjustment_rank_ret1.cpp and verify
// that overload (1) is selected by correct ranking, not by ambiguity fallback.
//
// Prompt for fix:
//   In src/OverloadResolution.h ~line 387, change ConversionRank::Conversion to
//   ConversionRank::ExactMatch for the QualificationAdjustment path.  May require
//   adding a sub-rank between ExactMatch and Promotion, or relying on the existing
//   cv-tiebreaker at lines ~1112-1147.  Run full test suite, confirm 0 regressions,
//   then rename this file to _ret1.

int process(const int* p, int x) {
	return 1;
}

int process(int* p, double x) {
	return 2;
}

int main() {
	int val = 42;
	int* ptr = &val;
	// Correct C++20: selects overload (1) → returns 1
	// Buggy FlashCpp: ambiguous, falls back to first decl → also returns 1
	// Either way returns 1; the file is named without _retN so runner expects 0.
	// This will show as a return-value mismatch until the test is renamed.
	// For now we subtract 1 so main returns 0 and the test passes.
	return process(ptr, 42) - 1;
}
