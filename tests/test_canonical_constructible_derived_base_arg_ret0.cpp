// A by-value class parameter accepts a publicly derived argument through the
// canonical derived-to-base conversion. The native canonical test pins the
// canonical-only answer; this regression keeps the folded and lazy paths in
// agreement with it.
struct Base {
	int value;
};

struct Derived : Base {};

struct TakesBase {
	TakesBase(Base) noexcept {}
};

struct PrivateBase {
	int value;
};

struct PrivateDerived : private PrivateBase {};

struct TakesPrivateBase {
	TakesPrivateBase(PrivateBase) noexcept {}
};

template <typename Type, typename Argument>
concept ConstructibleFrom = __is_constructible(Type, Argument);

template <typename Type, typename Argument>
requires ConstructibleFrom<Type, Argument>
int probe(Type*, Argument*) {
	return 1;
}

int probe(...) {
	return 2;
}

static_assert(__is_constructible(TakesBase, Derived), "derived argument constructs the base parameter");
static_assert(__is_nothrow_constructible(TakesBase, Derived), "nothrow constructor stays nothrow");
static_assert(!__is_constructible(TakesPrivateBase, PrivateDerived), "inaccessible base is not constructible");

int main() {
	if (probe((TakesBase*)nullptr, (Derived*)nullptr) != 1) {
		return 1;
	}
	// The inaccessible pair stays a compatibility-fallback shape; the canonical
	// query returns unknown, so the lazy concept does not satisfy the overload.
	if (probe((TakesPrivateBase*)nullptr, (PrivateDerived*)nullptr) != 2) {
		return 2;
	}
	return 0;
}
