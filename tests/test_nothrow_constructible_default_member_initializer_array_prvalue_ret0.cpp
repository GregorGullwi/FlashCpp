// An array member's default member initializer whose element is a prvalue of the
// element type (T{} or an empty brace {}) default-constructs that element by
// guaranteed copy elision. Before this was fixed, the nothrow walk only inspected
// elements with a resolved constructor, so an element whose default constructor
// can throw was ignored and the record was reported nothrow-constructible.
struct ThrowingDefaultCtor {
	ThrowingDefaultCtor() : tag(0) {}
	int tag;
};

struct NothrowDefaultCtor {
	NothrowDefaultCtor() noexcept : tag(0) {}
	int tag;
};

// A class whose implicit default construction runs a throwing subobject.
struct ThrowingSubobject {
	ThrowingDefaultCtor inner;
};

// Native members are mixed in; only the class-type elements can throw.
struct ThrowingPrvalueArray {
	int tag;
	ThrowingDefaultCtor members[2] = {ThrowingDefaultCtor{}, ThrowingDefaultCtor{}};
};

struct NothrowPrvalueArray {
	int tag;
	NothrowDefaultCtor members[2] = {NothrowDefaultCtor{}, NothrowDefaultCtor{}};
};

// The empty-brace spelling default-constructs each element the same way.
struct ThrowingBraceDefaultArray {
	ThrowingDefaultCtor members[2] = {{}, {}};
};

struct NothrowBraceDefaultArray {
	NothrowDefaultCtor members[2] = {{}, {}};
};

// A prvalue of a class whose own implicit default construction is throwing.
struct ThrowingNestedArray {
	ThrowingSubobject members[2] = {ThrowingSubobject{}, ThrowingSubobject{}};
};

static_assert(__is_constructible(ThrowingPrvalueArray), "a throwing element default constructor is still constructible");
static_assert(!__is_nothrow_constructible(ThrowingPrvalueArray), "a throwing element prvalue makes default construction throwing");
static_assert(__is_nothrow_constructible(NothrowPrvalueArray), "a noexcept element prvalue keeps default construction non-throwing");
static_assert(!__is_nothrow_constructible(ThrowingBraceDefaultArray), "an empty brace element makes default construction throwing");
static_assert(__is_nothrow_constructible(NothrowBraceDefaultArray), "an empty brace noexcept element keeps default construction non-throwing");
static_assert(!__is_nothrow_constructible(ThrowingNestedArray), "a prvalue whose subobject throws makes default construction throwing");

int main() {
	ThrowingPrvalueArray value{};
	return value.tag == 0 && value.members[1].tag == 0 ? 0 : 1;
}
