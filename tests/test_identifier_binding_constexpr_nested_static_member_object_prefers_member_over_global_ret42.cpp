// Phase 5 regression: nested constexpr member access should respect
// parser-bound static member objects before falling back to unqualified lookup.

struct Leaf {
	int value;
	constexpr Leaf(int v) : value(v) {}
};

struct Node {
	Leaf leaf;
	constexpr Node(int v) : leaf(v) {}
};

constexpr Node inner(5);

struct Outer {
	static constexpr Node inner{42};
	static constexpr int selected = inner.leaf.value;
	static_assert(selected == 42, "inner.leaf.value should use Outer::inner");
};

int main() {
	return 42;
}