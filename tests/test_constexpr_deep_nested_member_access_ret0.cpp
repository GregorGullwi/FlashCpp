struct DeepLeaf {
	int value;
	constexpr DeepLeaf(int v) : value(v) {}
};

struct DeepInner {
	DeepLeaf leaf;
	constexpr DeepInner(int v) : leaf(v) {}
};

struct DeepMiddle {
	DeepInner inner;
	constexpr DeepMiddle(int v) : inner(v) {}
};

struct DeepOuter {
	DeepMiddle middle;
	constexpr DeepOuter(int v) : middle(v) {}
};

constexpr DeepOuter global_outer(42);
static_assert(global_outer.middle.inner.leaf.value == 42);

int main() {
	return global_outer.middle.inner.leaf.value == 42 ? 0 : 1;
}
