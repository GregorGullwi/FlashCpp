// A conversion function returning an lvalue reference binds the target
// reference directly; the value-producing conversion function would materialize
// a temporary. Direct binding wins even when the reference result needs a
// derived-to-base adjustment and the value result is an exact match.
struct BasePart {
	int value;
};

struct DerivedPart : BasePart {
	int extra;
};

int by_value_calls = 0;
DerivedPart derived_storage{11, 12};

struct DerivedSource {
	operator BasePart() {
		++by_value_calls;
		return BasePart{5};
	}

	operator DerivedPart&() { return derived_storage; }
};

int main() {
	DerivedSource source;
	const BasePart& alias = source;
	return alias.value == 11 && by_value_calls == 0 ? 24 : 1;
}
