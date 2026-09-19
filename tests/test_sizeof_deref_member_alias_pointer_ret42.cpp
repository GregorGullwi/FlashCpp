// Regression: sizeof/alignof on a dereferenced pointer whose type comes from an
// instantiated member alias that captures the enclosing class-template
// parameter. The member alias materializer resolved the owner argument to its
// builtin spelling but never bound the concrete native TypeInfo, so the
// declared alias type kept the dependent Owner placeholder as the pointee and
// sizeof(*p) / sizeof(p[0]) fell back to the 8-byte pointer size.
//
// Shapes covered ([expr.sizeof]):
//   - owner is a builtin scalar of several widths (int / short / unsigned char):
//     sizeof(*p), sizeof(p[0])
//   - owner is a struct with a 12-byte layout, distinct from the pointer size,
//     so a pointee/pointer mix-up cannot cancel out: sizeof(*p), sizeof(p[0])
//   - non-member alias template and plain int* stay correct
//   - constant-expression forms via static_assert on named entities

template <class Owner>
struct Captures {
	template <class Value>
	using Pointer = Owner*;
};

template <class V>
using FreeAlias = unsigned char*;

struct Wide {
	int first;
	int second;
	int third;
};

using IntPointer = typename Captures<int>::template Pointer<char>;
using ShortPointer = typename Captures<short>::template Pointer<int>;
using UCharPointer = typename Captures<unsigned char>::template Pointer<char>;
using WidePointer = typename Captures<Wide>::template Pointer<char>;

static_assert(sizeof(IntPointer) == sizeof(void*));
static_assert(sizeof(ShortPointer) == sizeof(void*));
static_assert(sizeof(UCharPointer) == sizeof(void*));
static_assert(sizeof(WidePointer) == sizeof(void*));

int main() {
	int value = 0;
	short short_value = 0;
	unsigned char byte_value = 0;
	Wide wide_value = {7, 9, 3};

	IntPointer int_pointer = &value;
	ShortPointer short_pointer = &short_value;
	UCharPointer byte_pointer = &byte_value;
	WidePointer wide_pointer = &wide_value;
	FreeAlias<char> free_alias_pointer = &byte_value;
	int* plain_pointer = &value;

	*int_pointer = 41;
	*short_pointer = 17;
	*byte_pointer = 3;
	wide_pointer->first = 41;
	wide_pointer->second = 1;

	static_assert(sizeof(*int_pointer) == sizeof(int));
	static_assert(sizeof(*short_pointer) == sizeof(short));
	static_assert(sizeof(*byte_pointer) == sizeof(unsigned char));
	static_assert(sizeof(*wide_pointer) == sizeof(Wide));
	static_assert(sizeof(*plain_pointer) == sizeof(int));
	static_assert(sizeof(*free_alias_pointer) == sizeof(unsigned char));

	static_assert(sizeof(int_pointer[0]) == sizeof(int));
	static_assert(sizeof(short_pointer[0]) == sizeof(short));
	static_assert(sizeof(byte_pointer[0]) == sizeof(unsigned char));
	static_assert(sizeof(wide_pointer[0]) == sizeof(Wide));
	static_assert(sizeof(wide_pointer[0]) == 12);

	static_assert(alignof(*int_pointer) == alignof(int));
	static_assert(alignof(*short_pointer) == alignof(short));
	static_assert(alignof(*wide_pointer) == alignof(Wide));
	static_assert(alignof(*wide_pointer) == 4);

	return (*int_pointer == 41 && *short_pointer == 17 && *byte_pointer == 3 &&
			wide_pointer->first == 41 && wide_pointer->second == 1 &&
			sizeof(*wide_pointer) == 12)
			   ? 42
			   : 0;
}
