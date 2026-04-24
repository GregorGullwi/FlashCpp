// Regression test for C++20 [over.ics.rank] p3.3.1.4:
// binding an rvalue to T&& is preferred over binding it to const T&.
// Before the fix the conversion rank for rvalue→const T& was set to
// ConversionRank::Conversion, making it lose to other Conversion-rank
// candidates (e.g. f(const int&) would incorrectly tie with f(long)).
// With the fix the rank is QualificationAdjustment, which is strictly
// between ExactMatch and Promotion — correct ordering is preserved.

int f(int&&) { return 1; }
int f(const int&) { return 2; }

struct S { int val; };
int g(S&&) { return 10; }
int g(const S&) { return 20; }

int main() {
int x = 5;
// Cast to rvalue: should prefer f(int&&)
int r1 = f((int&&)x);
if (r1 != 1)
return r1;

// Lvalue: should prefer f(const int&)
int r2 = f(x);
if (r2 != 2)
return r2;

// Rvalue struct: should prefer g(S&&)
S s{3};
int r3 = g((S&&)s);
if (r3 != 10)
return r3;

// Lvalue struct: should prefer g(const S&)
int r4 = g(s);
if (r4 != 20)
return r4;

return 0;
}
