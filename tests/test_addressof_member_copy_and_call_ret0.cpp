struct Leaf {
	int value;

	Leaf(int v = 0) : value(v) {}

	Leaf(const Leaf& other) : value(other.value + 3) {}
};

struct Inner {
	Leaf leaf;

	Inner(int value) : leaf(value) {}
};

struct Outer {
	Inner inner;
	int tail;

	Outer(int value, int tail_value) : inner(value), tail(tail_value) {}
};

int readLeaf(const Leaf* leaf) {
	return leaf->value;
}

int main() {
	Outer obj(10, 99);

	Leaf copied(obj.inner.leaf);
	if (copied.value != 13) {
		return 1;
	}

	if (readLeaf(&obj.inner.leaf) != 10) {
		return 2;
	}

	return obj.tail == 99 ? 0 : 3;
}
