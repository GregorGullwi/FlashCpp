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

int main() {
	return 0;
}
