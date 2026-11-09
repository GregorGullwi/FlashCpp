// [over.ics.user]/[class.conv.ctor]: an unrelated class argument can reach a
// by-value parameter through one non-explicit public converting constructor of
// the parameter class. Explicit, private, and ambiguous candidates stay
// fail-closed.
struct Source {
	int value;
};

struct Target {
	Target(Source) noexcept {}
};

struct Outer {
	Outer(Target) noexcept {}
};

struct ExplicitTarget {
	explicit ExplicitTarget(Source) {}
};

struct ExplicitOuter {
	ExplicitOuter(ExplicitTarget) {}
};

struct DeletedTarget {
	DeletedTarget(Source) = delete;
};

struct DeletedOuter {
	DeletedOuter(DeletedTarget) {}
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

static_assert(__is_constructible(Outer, Source), "public converting constructor is viable");
static_assert(__is_nothrow_constructible(Outer, Source), "nothrow target constructor stays nothrow");
static_assert(!__is_constructible(ExplicitOuter, Source), "explicit converting constructor is not implicit");
static_assert(!__is_constructible(DeletedOuter, Source), "deleted converting constructor is not viable");

int main() {
	if (probe((Outer*)nullptr, (Source*)nullptr) != 1) {
		return 1;
	}
	if (probe((ExplicitOuter*)nullptr, (Source*)nullptr) != 2) {
		return 2;
	}
	if (probe((DeletedOuter*)nullptr, (Source*)nullptr) != 2) {
		return 3;
	}
	return 0;
}
