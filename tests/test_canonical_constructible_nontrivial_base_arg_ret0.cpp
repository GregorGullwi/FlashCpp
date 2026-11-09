// A publicly derived by-value argument is accepted when the base publishes a
// usable public copy constructor, even when the base is not trivially copyable.
// The canonical query reads the base constructor schema instead of the
// trivially-copyable proxy.
struct NontrivialBase {
	NontrivialBase() {}
	NontrivialBase(const NontrivialBase&) {}
	int value;
};

struct Derived : NontrivialBase {};

struct TakesBase {
	TakesBase(NontrivialBase) noexcept {}
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

template <typename Type, typename Argument>
requires ConstructibleFrom<Type, Argument>
int probe_reference(Type*, Argument&&) {
	return 1;
}

int probe_reference(...) {
	return 2;
}

static_assert(__is_constructible(TakesBase, Derived), "non-trivial base accepts a derived rvalue");
static_assert(__is_nothrow_constructible(TakesBase, Derived), "nothrow constructor stays nothrow");
static_assert(__is_constructible(TakesBase, Derived&), "non-trivial base accepts a derived lvalue");

int main() {
	if (probe((TakesBase*)nullptr, (Derived*)nullptr) != 1) {
		return 1;
	}
	Derived derived{};
	if (probe_reference((TakesBase*)nullptr, derived) != 1) {
		return 2;
	}
	return 0;
}
