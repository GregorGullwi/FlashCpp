// A reference member initialized from a named object binds an lvalue, so the
// containing default construction inherits the reference member's non-trivial
// but nothrow initialization.

struct Referenced {
	int value = 0;
};

Referenced referenced_object{};

struct BoundReference {
	const Referenced& reference = referenced_object;
};

static_assert(__is_constructible(BoundReference));
static_assert(!__is_trivially_constructible(BoundReference));
static_assert(__is_nothrow_constructible(BoundReference));

int main() {
	BoundReference bound;
	return bound.reference.value == 0 ? 0 : 1;
}
