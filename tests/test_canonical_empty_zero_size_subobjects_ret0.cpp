struct EmptyType {};
struct EmptyZeroWidthBitfield {
	unsigned int : 0;
};
struct NonemptyType {
	int value;
};
struct EmptyDerived : EmptyType {};
struct NonemptyDerived : NonemptyType {};

static_assert(__is_empty(EmptyType));
static_assert(__is_empty(EmptyZeroWidthBitfield));
static_assert(__is_empty(EmptyDerived));
static_assert(!__is_empty(NonemptyType));
static_assert(!__is_empty(NonemptyDerived));

int main() {
	return 0;
}
