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
//
// KNOWN BUG: qualification adjustment (T*->const T*) is ranked as
// ConversionRank::Conversion instead of ExactMatch in buildConversionPlan
// (src/OverloadResolution.h ~line 387).  This makes the two candidates
// score (Conversion, ExactMatch) vs (ExactMatch, Conversion) — incomparable,
// causing spurious ambiguity.  FlashCpp correctly diagnoses this as a
// compile error: "Ambiguous call to overloaded function 'process'".
//
// This file is named _fail.cpp so CI expects the compile error.
//
// When fixed, rename to test_qualification_adjustment_rank_ret1.cpp and verify
// that overload (1) is selected by correct ranking (not ambiguity).
//
// Prompt for fix:
//   In src/OverloadResolution.h ~line 387, change ConversionRank::Conversion to
//   ConversionRank::ExactMatch for the QualificationAdjustment path.  May require
//   adding a sub-rank between ExactMatch and Promotion, or relying on the existing
//   cv-tiebreaker at lines ~1112-1147.  Run full test suite, confirm 0 regressions,
//   then rename this file to test_qualification_adjustment_rank_ret1.cpp.

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
