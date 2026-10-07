// A class with an implicit default constructor and a class-type array member
// whose default member initializer constructs each element must account for
// each element's constructor exception specification. Before this was fixed,
// the nothrow subobject walk skipped every array member, so a class whose array
// element construction can throw was reported nothrow-constructible.
struct ThrowingCtor {
	ThrowingCtor(int value) : value(value) {}
	int value;
};

struct NothrowCtor {
	NothrowCtor(int value) noexcept : value(value) {}
	int value;
};

// A native member is mixed in; only the class-type array element can throw.
struct ThrowingArray {
	int tag;
	ThrowingCtor members[2] = {ThrowingCtor(1), ThrowingCtor(2)};
};

struct NothrowArray {
	int tag;
	NothrowCtor members[2] = {NothrowCtor(1), NothrowCtor(2)};
};

// A brace-list element spelling selects the same element constructor.
struct ThrowingBraceArray {
	ThrowingCtor members[2] = {{1}, {2}};
};

struct NothrowBraceArray {
	NothrowCtor members[2] = {{1}, {2}};
};

static_assert(__is_constructible(ThrowingArray), "a throwing array member is still constructible");
static_assert(!__is_nothrow_constructible(ThrowingArray), "a throwing array element makes default construction throwing");
static_assert(__is_nothrow_constructible(NothrowArray), "a noexcept array element keeps default construction non-throwing");
static_assert(!__is_nothrow_constructible(ThrowingBraceArray), "a throwing brace array element makes default construction throwing");
static_assert(__is_nothrow_constructible(NothrowBraceArray), "a noexcept brace array element keeps default construction non-throwing");

// Any default member initializer makes the default constructor non-trivial.
static_assert(!__is_trivially_constructible(ThrowingArray), "an array member initializer makes default construction non-trivial");
static_assert(!__is_trivially_constructible(NothrowArray), "an array member initializer makes default construction non-trivial");

int main() {
	ThrowingArray value{};
	return value.tag == 0 && value.members[1].value == 2 ? 0 : 1;
}
