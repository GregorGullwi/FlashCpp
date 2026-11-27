// [dcl.mptr] with [expr.prim.id.qual]: a decltype-specifier followed by `::`
// is a nested-name-specifier, so `int decltype(c)::*p` is a pointer to an int
// member of the type of `c`. The owner identity comes from the decltype type,
// so the member-pointer value selects the same member as `int C::*`.
// The ptr-operator must work in a named declarator, a parenthesized declarator,
// and a type-id.
struct C {
	int member;
	long other;
};

int main() {
	C c{0, 0};
	int decltype(c)::*p = &C::member;
	c.*p = 41;
	if (c.member != 41) {
		return 1;
	}
	if (c.*p != 41) {
		return 2;
	}

	int (decltype(c)::*grouped) = &C::member;
	c.*grouped = 7;
	if (c.member != 7) {
		return 3;
	}

	// The pointee is int: assigning through the member pointer must not change
	// the layout or select a different member.
	int replacement = 23;
	c.*p = replacement;
	if (c.member != 23 || c.other != 0) {
		return 4;
	}

	// A `long` decl-specifier is consumed as a type before the decltype owner,
	// so the decltype-specifier must stay with the ptr-operator.
	long decltype(c)::*wide = &C::other;
	c.*wide = 13;
	if (c.other != 13) {
		return 5;
	}

	// The deduced type may be cv-qualified; the owner is still the class.
	volatile C volatile_object{0, 0};
	int decltype(volatile_object)::*volatile_owner = &C::member;
	c.*volatile_owner = 31;
	if (c.member != 31) {
		return 6;
	}

	if (sizeof(int decltype(c)::*) != sizeof(int C::*)) {
		return 7;
	}
	if (sizeof(int (decltype(c)::*)) != sizeof(int C::*)) {
		return 8;
	}
	if (sizeof(int decltype(c)::*) != 8 || sizeof(long decltype(c)::*) != 8) {
		return 9;
	}
	return 0;
}
