// Regression: a sema-normalized ternary whose branches involve pointer
// arithmetic must compute a sema-owned ternary result type.
//
// Before this fix, sema's `inferExpressionType` returned `nullopt` for any
// binary operator with pointer operands (including `pointer + integral` and
// `pointer - pointer`), so the ternary's branch-type comparison short-circuited
// to nullopt. After commit 4e872b6 made codegen require an exact ternary
// result type for sema-normalized function bodies, this regressed
// `<typeinfo>`'s `name()` member, whose body is exactly:
//
//     return __name[0] == '*' ? __name + 1 : __name;
//
// and produced "Sema-normalized ternary expression missing exact result type"
// during codegen.  Sema now models pointer arithmetic per C++20 [expr.add]:
//   T* + integral -> T*;  integral + T* -> T*;
//   T* - integral -> T*;  T* - T*       -> ptrdiff_t.
//
// All of the cases below are routed through string-literal pointer arguments
// to avoid the (independent, pre-existing) array-to-pointer decay gap at
// function call sites.
const char* skip_star(const char* p) {
	return p[0] == '*' ? p + 1 : p;
}

const char* maybe_back_one(const char* p, bool back) {
	// Pointer minus integral inside a ternary.
	return back ? p - 1 : p;
}

long ptr_diff_via_ternary(const char* a, const char* b, bool swap) {
	// Pointer minus pointer (ptrdiff_t) inside a ternary.
	return swap ? b - a : a - b;
}

int main() {
	const char* s1 = "*hidden";
	const char* s2 = "plain";
	if (skip_star(s1)[0] != 'h') return 1;
	if (skip_star(s2)[0] != 'p') return 2;

	// Walk forward by two and use the back-one branch.
	const char* base = s2;
	const char* mid  = base + 2;
	if (maybe_back_one(mid, true)[0]  != 'l') return 3; // "plain"[1]
	if (maybe_back_one(mid, false)[0] != 'a') return 4; // "plain"[2]

	const char* end = base + 5; // points at the trailing nul terminator
	if (ptr_diff_via_ternary(base, end, false) != -5) return 5;
	if (ptr_diff_via_ternary(base, end, true)  !=  5) return 6;
	return 0;
}
