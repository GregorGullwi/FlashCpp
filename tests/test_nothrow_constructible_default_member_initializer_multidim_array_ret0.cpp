// A class with an implicit default constructor and a multidimensional
// class-type array member whose default member initializer constructs each
// element must account for each element's constructor exception specification.
// Before this was fixed, only single-dimension array members contributed, so a
// multidimensional array element that can throw was reported non-throwing. Both
// the flat brace-elision form and the nested row form are covered.
struct ThrowingCtor {
	ThrowingCtor(int value) : value(value) {}
	int value;
};

struct NothrowCtor {
	NothrowCtor(int value) noexcept : value(value) {}
	int value;
};

struct FlatThrowing {
	int tag;
	ThrowingCtor grid[2][2] = {ThrowingCtor(1), ThrowingCtor(2), ThrowingCtor(3), ThrowingCtor(4)};
};

struct FlatNothrow {
	int tag;
	NothrowCtor grid[2][2] = {NothrowCtor(1), NothrowCtor(2), NothrowCtor(3), NothrowCtor(4)};
};

struct NestedThrowing {
	ThrowingCtor grid[2][2] = {{ThrowingCtor(1), ThrowingCtor(2)}, {ThrowingCtor(3), ThrowingCtor(4)}};
};

struct NestedNothrow {
	NothrowCtor grid[2][2] = {{NothrowCtor(1), NothrowCtor(2)}, {NothrowCtor(3), NothrowCtor(4)}};
};

struct TripleThrowing {
	ThrowingCtor grid[2][1][2] = {{{ThrowingCtor(1), ThrowingCtor(2)}}, {{ThrowingCtor(3), ThrowingCtor(4)}}};
};

static_assert(__is_constructible(FlatThrowing), "a throwing multidimensional array member is still constructible");
static_assert(!__is_nothrow_constructible(FlatThrowing), "a flat multidimensional throwing element makes default construction throwing");
static_assert(__is_nothrow_constructible(FlatNothrow), "a flat multidimensional noexcept element keeps default construction non-throwing");
static_assert(!__is_nothrow_constructible(NestedThrowing), "a nested multidimensional throwing element makes default construction throwing");
static_assert(__is_nothrow_constructible(NestedNothrow), "a nested multidimensional noexcept element keeps default construction non-throwing");
static_assert(!__is_nothrow_constructible(TripleThrowing), "a three-dimensional throwing element makes default construction throwing");

int main() {
	FlatThrowing value{};
	return value.tag == 0 ? 0 : 1;
}
