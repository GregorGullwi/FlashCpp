struct Target {
	int value;
	int pad0;
	int pad1;
	int pad2;

	explicit Target(int x) : value(x + 1000), pad0(0), pad1(0), pad2(0) {}
	Target(double d, int bias = 2)
		: value(static_cast<int>(d) + bias), pad0(10), pad1(20), pad2(30) {}
};

Target make(int source) {
	return source;
}

int main() {
	Target t = make(40);
	return t.value + t.pad0 + t.pad1 + t.pad2 - 60;
}
