// Test: qualification adjustment should rank as ExactMatch, not Conversion.
//
// Two overloads:
//   int process(const int* p, int x);    // (1)
//   int process(int* p, double x);       // (2)
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
// causing spurious ambiguity.  FlashCpp currently falls through to the
// first-declared overload, so the test happens to return 1 today, but
// for the wrong reason (ambiguity fallback, not correct ranking).
//
// When fixed: rename this file to test_qualification_adjustment_rank_ret1.cpp
// (same name — the return value is correct, but the ranking path should change).
//
// Prompt for fix:
//   Change ConversionRank::Conversion to ConversionRank::ExactMatch for the
//   T*->const T* qualification adjustment path in buildConversionPlan
//   (src/OverloadResolution.h, StandardConversionKind::QualificationAdjustment).
//   Consider adding a sub-rank or using the existing cv-tiebreaker so that
//   identity (T*->T*) still beats qualification adjustment (T*->const T*)
//   within the ExactMatch category.  Run full test suite, confirm 0 regressions.

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
