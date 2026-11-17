// Converting-constructor candidates must rank by their argument conversion
// plan, not by counting viable constructors. A better nested constructor makes
// the outer construction viable, and a better deleted constructor rejects it.

struct ScalarTarget {
	ScalarTarget(int) noexcept {}
	ScalarTarget(long) noexcept {}
};

struct ScalarOuter {
	ScalarOuter(ScalarTarget) noexcept {}
};

struct Base {};
struct Derived : Base {};

struct ReferenceTarget {
	ReferenceTarget(Derived) noexcept {}
	ReferenceTarget(Base) noexcept {}
};

struct ReferenceOuter {
	ReferenceOuter(const ReferenceTarget&) noexcept {}
};

struct DeletedTarget {
	DeletedTarget(int) = delete;
	DeletedTarget(long) noexcept {}
};

struct DeletedOuter {
	DeletedOuter(DeletedTarget) noexcept {}
};

struct AccessTarget {
private:
	AccessTarget(Derived) noexcept {}
public:
	AccessTarget(Base) noexcept {}
};

struct AccessOuter {
	AccessOuter(AccessTarget) noexcept {}
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

static_assert(__is_constructible(ScalarOuter, int), "exact nested constructor wins");
static_assert(__is_constructible(ScalarOuter, long), "converting nested constructor wins");
static_assert(__is_nothrow_constructible(ScalarOuter, int), "exact nested constructor stays nothrow");
static_assert(__is_constructible(ReferenceOuter, Derived), "derived nested constructor wins over base");
static_assert(__is_nothrow_constructible(ReferenceOuter, Derived), "selected nested constructor stays nothrow");
static_assert(!__is_constructible(DeletedOuter, int), "deleted exact nested constructor rejects");
static_assert(__is_constructible(DeletedOuter, long), "viable worse nested constructor remains usable");
static_assert(!__is_constructible(AccessOuter, Derived), "inaccessible exact nested constructor rejects");
static_assert(__is_constructible(AccessOuter, Base), "accessible worse nested constructor remains usable");

int main() {
	if (probe((ScalarOuter*)nullptr, (int*)nullptr) != 1) {
		return 1;
	}
	if (probe((ReferenceOuter*)nullptr, (Derived*)nullptr) != 1) {
		return 2;
	}
	if (probe((DeletedOuter*)nullptr, (int*)nullptr) != 2) {
		return 3;
	}
	if (probe((AccessOuter*)nullptr, (Derived*)nullptr) != 2) {
		return 4;
	}
	return 0;
}
