struct Base {
	int base;

	int addToBase(int value) {
		return base + value;
	}
};

struct Derived : Base {
	int compute() {
		base = 40;
		return addToBase(2);
	}
};

int main() {
	Derived d;
	return d.compute() - 42;
}
