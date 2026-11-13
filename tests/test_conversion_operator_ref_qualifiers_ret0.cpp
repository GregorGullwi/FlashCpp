// Conversion operators with ref qualifiers must be selected by the source value
// category: lvalues only bind to & or unqualified operators, while rvalues bind
// to && or unqualified operators. Constructibility uses the same rule.

struct RefQualified {
	operator int() & { return 1; }
	operator int() && { return 2; }
};

struct RvalueOnly { operator int() && { return 3; } };

struct ConstVolatileOp { operator int() const volatile { return 4; } };

struct NonConstVirtualBase { operator int() { return 5; } };
struct NonConstVirtualDerived : virtual NonConstVirtualBase {};

struct Target { Target(int) {} };

static_assert(!__is_constructible(Target, RvalueOnly&));
static_assert(__is_constructible(Target, RvalueOnly&&));

int main() {
	RefQualified ref;
	if (static_cast<int>(ref) != 1) {
		return 1;
	}
	if (static_cast<int>(RefQualified{}) != 2) {
		return 2;
	}
	if (static_cast<int>(RvalueOnly{}) != 3) {
		return 3;
	}
	const volatile ConstVolatileOp cv;
	if (static_cast<int>(cv) != 4) {
		return 4;
	}
	NonConstVirtualDerived derived;
	if (static_cast<int>(derived) != 5) {
		return 5;
	}
	return 0;
}
