// Regression test for: std::rel_ops operators incorrectly instantiated via
// unqualified template-registry lookup.
//
// Including <utility> also pulls in <bits/stl_relops.h>, which defines
// std::rel_ops::operator<=, operator>, operator>=, and operator!= as function
// templates taking `const _Tp&` arguments for any type _Tp.
//
// Per C++20 [basic.lookup.argdep]/2, std::rel_ops is NOT an associated
// namespace of std::pair<int,float>.  FlashCpp must not instantiate
// std::rel_ops::operator<=<pair<int,float>> (or the analogous !=, >, >=
// overloads) when compiling an expression that merely constructs a pair.
// Previously, those instantiations were added to the codegen queue unconditionally
// and then failed at IR-generation time because pair<int,float> has no operator<.
#include <utility>

int main() {
	std::pair<int, float> p(42, 3.14f);
	std::pair<int, float> q(7, 1.0f);

	// Construct and access members – sufficient to previously trigger the bug.
	int result = (p.first == 42) ? 0 : 1;

	// Also use a pair comparison that IS valid in C++20 (via operator<=>).
	bool same = (p.first == q.first);
	result += same ? 1 : 0;

	return result; // returns 0 when p.first == 42
}
