// Test: mixed qualification-adjustment + conversion overloads are ambiguous.
//
// Two overloads:
//   int process(const int* p, int x);    // (1)
//   int process(int* p, double x);       // (2)
//
// Calling process(ptr, 42) where ptr is int*:
//
// Per C++20 [over.ics.rank]/3.2.6:
//   Candidate (1): arg1 int*->const int* = QualificationAdjustment, arg2 int->int = identity
//   Candidate (2): arg1 int*->int* = identity (ExactMatch), arg2 int->double = Conversion
//
// Candidate (2) is better on arg1 (ExactMatch < QualificationAdjustment).
// Candidate (1) is better on arg2 (ExactMatch < Conversion).
// Neither dominates -> ambiguous.
// Clang confirms: "call to 'process' is ambiguous".

int process(const int* p, int x) {
return 1;
}

int process(int* p, double x) {
return 2;
}

int main() {
int val = 42;
int* ptr = &val;
return process(ptr, 42);
}
