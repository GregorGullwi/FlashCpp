// A class template constructor's dependent noexcept(expr) operand must be
// re-evaluated for each specialization. Before this was fixed the parser kept
// the keyword-present answer and dropped the operand, so the pattern's default
// was copied into every specialization and __is_nothrow_constructible reported
// the throwing specialization as non-throwing.
template <bool Enabled>
struct Guarded {
	Guarded() noexcept(!Enabled) {}
	int tag = 0;
};

// A non-type template parameter folded through the same substitution path.
template <int Mode>
struct Coded {
	Coded() noexcept(Mode == 0) {}
};

// A member whose own specialization selects the throwing constructor.
template <bool Enabled>
struct Holder {
	int tag = 0;
	Guarded<Enabled> member;
};

static_assert(!__is_nothrow_constructible(Guarded<true>), "noexcept(false) when enabled");
static_assert(__is_nothrow_constructible(Guarded<false>), "noexcept(true) when disabled");
static_assert(__is_nothrow_constructible(Coded<0>), "mode zero is noexcept");
static_assert(!__is_nothrow_constructible(Coded<1>), "non-zero mode can throw");
static_assert(!__is_nothrow_constructible(Holder<true>), "a throwing member specialization makes the holder throwing");
static_assert(__is_nothrow_constructible(Holder<false>), "a noexcept member specialization keeps the holder noexcept");

int main() {
	Guarded<true> guarded{};
	Holder<false> holder{};
	return guarded.tag == 0 && holder.tag == 0 ? 0 : 1;
}
