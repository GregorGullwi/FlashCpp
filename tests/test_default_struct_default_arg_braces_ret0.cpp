struct Leaf {
	int value;
	Leaf(int v = 7) : value(v) {}
};

int readLeaf(Leaf leaf = {}) {
	return leaf.value;
}

int main() {
	return readLeaf() == 7 ? 0 : 1;
}
