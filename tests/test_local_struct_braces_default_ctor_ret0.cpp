struct Leaf {
	int value;
	Leaf(int v = 7) : value(v) {}
};

int main() {
	Leaf leaf = {};
	return leaf.value == 7 ? 0 : 1;
}
