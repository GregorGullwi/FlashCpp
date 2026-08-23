// Regression: static pointer-to-array members keep scalar pointer storage and
// their pointee bounds (C++20 [dcl.ptr]/1, [dcl.arr]/1, [expr.sizeof]).
// Before the fix the static-member path scaled storage by the pointee bounds.
//
// NOTE: out-of-line definitions of qualified static members spelled with
// parenthesized declarators (long (*Registry::table)[2][4] = ...;) are not
// parsed yet; this test uses in-class constexpr members.

struct Registry {
	static constexpr long (*table)[2][4] = nullptr;
	static constexpr int (*cursor)[3] = nullptr;
};

long rows[2][4] = {{0, 1, 2, 3}, {4, 5, 6, 7}};

template <typename T>
struct Holder {
	static constexpr T (*slot)[2][2] = nullptr;
};

int main() {
	if (sizeof(Registry::table) != 8) {
		return 1;
	}
	if (sizeof(Registry::cursor) != 8) {
		return 2;
	}

	Holder<int> h;
	if (sizeof(h.slot) != 8) {
		return 4;
	}
	return 0;
}
