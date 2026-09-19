// Regression: T{} / T() on a typedef or alias whose terminal is a pointer must
// value-initialize a pointer ([expr.type.conv], [dcl.init]), not construct the
// pointee class. using WidePtr = Wide* used to miss the functional-cast path
// because the terminal is a struct, then fell into a UserDefined placeholder
// (size 0) and IR looked up constructor info for the alias name.

struct Wide {
	int first;
	int second;
	int third;
};

struct Tiny {
	char tag;
};

using WidePtr = Wide*;
using TinyPtr = Tiny*;
using IntPtr = int*;
using UCharPtr = unsigned char*;

template <class Owner>
struct Captures {
	template <class Value>
	using Pointer = Owner*;
};

using MemberWidePtr = typename Captures<Wide>::template Pointer<char>;
using MemberShortPtr = typename Captures<short>::template Pointer<int>;

static_assert(sizeof(WidePtr{}) == sizeof(void*));
static_assert(sizeof(WidePtr()) == sizeof(void*));
static_assert(sizeof(IntPtr{}) == sizeof(void*));
static_assert(sizeof(UCharPtr{}) == sizeof(void*));
static_assert(sizeof(TinyPtr{}) == sizeof(void*));
static_assert(sizeof(MemberWidePtr{}) == sizeof(void*));
static_assert(sizeof(MemberShortPtr{}) == sizeof(void*));
static_assert(sizeof(WidePtr{}) != sizeof(Wide));
static_assert(sizeof(Wide) == 12);

int main() {
	WidePtr wide_prvalue = WidePtr{};
	TinyPtr tiny_prvalue = TinyPtr();
	IntPtr int_prvalue = IntPtr{};
	MemberWidePtr member_prvalue = MemberWidePtr{};

	return (wide_prvalue == nullptr && tiny_prvalue == nullptr &&
			int_prvalue == nullptr && member_prvalue == nullptr &&
			sizeof(WidePtr{}) == 8 && sizeof(MemberWidePtr{}) == 8)
			   ? 42
			   : 0;
}
