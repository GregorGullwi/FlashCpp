// A record-property trait used in a folded static member initializer must be
// evaluated against the concrete instantiation, not the pattern's deferred base.

struct FoldedTraitBase {};
struct FoldedTraitNonEmpty {
	int value;
};

template <class T> struct FoldedTraitDerived : T {
	static constexpr bool empty = __is_empty(FoldedTraitDerived);
	static constexpr bool trivially_destructible = __is_trivially_destructible(FoldedTraitDerived);
};

static_assert(FoldedTraitDerived<FoldedTraitBase>::empty);
static_assert(!FoldedTraitDerived<FoldedTraitNonEmpty>::empty);
static_assert(FoldedTraitDerived<FoldedTraitBase>::trivially_destructible);

int main() {
	const bool empty_base = FoldedTraitDerived<FoldedTraitBase>::empty;
	const bool empty_non_base = FoldedTraitDerived<FoldedTraitNonEmpty>::empty;
	const bool trivial = FoldedTraitDerived<FoldedTraitBase>::trivially_destructible;
	return empty_base && !empty_non_base && trivial ? 0 : 1;
}
