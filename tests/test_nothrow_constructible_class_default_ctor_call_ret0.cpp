// A class-typed default parameter initialized by a fully specified noexcept
// constructor call proves the nothrow construction fact. A throwing nested
// constructor or a throwing temporary destructor keeps the answer false.
struct NothrowArg {
	NothrowArg(int) noexcept {}
};

struct ThrowingArg {
	ThrowingArg(int) noexcept(false) {}
};

struct ThrowingDtorArg {
	ThrowingDtorArg(int) noexcept {}
	~ThrowingDtorArg() noexcept(false) {}
};

struct NestedNothrowDefaults {
	NestedNothrowDefaults(int, NothrowArg = NothrowArg(1)) noexcept {}
};

struct NestedThrowingDefaults {
	NestedThrowingDefaults(int, ThrowingArg = ThrowingArg(1)) noexcept {}
};

struct OuterNestedNothrow {
	OuterNestedNothrow(int, NestedNothrowDefaults = NestedNothrowDefaults(1)) noexcept {}
};

struct OuterNestedThrowing {
	OuterNestedThrowing(int, NestedThrowingDefaults = NestedThrowingDefaults(1)) noexcept {}
};

struct ConvertedArg {
	ConvertedArg(int) noexcept {}
};

struct NestedClassParam {
	NestedClassParam(ConvertedArg) noexcept {}
};

struct OuterClassParamDefault {
	OuterClassParamDefault(int, NestedClassParam = NestedClassParam(1)) noexcept {}
};

struct NothrowDefault {
	NothrowDefault(int, NothrowArg = NothrowArg(1)) noexcept {}
};

struct NothrowRefDefault {
	NothrowRefDefault(int, const NothrowArg& = NothrowArg(1)) noexcept {}
};

struct ThrowingDefault {
	ThrowingDefault(int, ThrowingArg = ThrowingArg(1)) noexcept {}
};

struct ThrowingDtorDefault {
	ThrowingDtorDefault(int, ThrowingDtorArg = ThrowingDtorArg(1)) noexcept {}
};

static_assert(__is_nothrow_constructible(NothrowDefault, int), "noexcept nested constructor");
static_assert(__is_nothrow_constructible(NothrowRefDefault, int), "noexcept nested constructor bound to a reference");
static_assert(!__is_nothrow_constructible(ThrowingDefault, int), "throwing nested constructor");
static_assert(!__is_nothrow_constructible(ThrowingDtorDefault, int), "throwing temporary destructor");
static_assert(__is_nothrow_constructible(OuterNestedNothrow, int), "omitted nested default is proven nothrow");
static_assert(!__is_nothrow_constructible(OuterNestedThrowing, int), "omitted nested default is known throwing");
static_assert(__is_nothrow_constructible(OuterClassParamDefault, int), "class parameter conversion is proven nothrow");

int main() {
	return 0;
}
