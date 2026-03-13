struct Leaf {
	int value;

	Leaf(int v = 0) : value(v) {}

	Leaf(const Leaf& other) : value(other.value + 3) {}
};

struct Inner {
	Leaf leaf;
};

struct Outer {
	Inner inner;
	int tail;
};

int readLeaf(const Leaf* leaf) {
	return leaf->value;
}

int bumpLeaf(Leaf* leaf) {
	leaf->value += 4;
	return leaf->value;
}

int main() {
	Outer obj{{Leaf(10)}, 99};

	Leaf copied(obj.inner.leaf);
	if (copied.value != 13) {
		return 1;
	}

	if (readLeaf(&obj.inner.leaf) != 10) {
		return 2;
	}

	if (bumpLeaf(&obj.inner.leaf) != 14) {
		return 3;
	}

	if (obj.inner.leaf.value != 14) {
		return 4;
	}

	return obj.tail == 99 ? 0 : 5;
}
