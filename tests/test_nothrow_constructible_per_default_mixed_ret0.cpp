// Per-default exception facts must classify each omitted default independently:
// supplying a known-throwing default argument does not taint a later omitted
// nothrow default, while omitting the throwing default still does.
struct MixedThrowingArg {
	MixedThrowingArg(int) noexcept(false) {}
};

struct MixedDefaults {
	MixedDefaults(int, MixedThrowingArg = MixedThrowingArg(1), int = 42) noexcept {}
};

static_assert(__is_nothrow_constructible(MixedDefaults, int, MixedThrowingArg), "supplied throwing default does not taint an omitted nothrow one");
static_assert(!__is_nothrow_constructible(MixedDefaults, int), "an omitted throwing default still taints");
static_assert(__is_nothrow_constructible(MixedDefaults, int, MixedThrowingArg, int), "all parameters supplied");

int main() {
	return 0;
}
