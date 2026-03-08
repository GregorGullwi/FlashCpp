// Phase 5 regression: constexpr member access should respect parser-bound
// static member objects before falling back to unqualified lookup.

struct Inner {
	int value;
	constexpr Inner(int v) : value(v) {}
};

constexpr Inner inner(5);

struct Outer {
	static constexpr Inner inner{42};
	static constexpr int selected = inner.value;
	static_assert(selected == 42, "inner.value should use Outer::inner");
};

int main() {
	return 42;
}