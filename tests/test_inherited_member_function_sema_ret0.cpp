struct Box {
	int value;
};

struct Base {
	int base;
	static int shared_value;

	Box getValue() const {
		Box box;
		box.value = 42;
		return box;
	}

	int addToBase(int value) {
		return base + value;
	}
};

int Base::shared_value = 42;

struct Derived : Base {
	int marker;

	int compute() {
		base = 40;
		return addToBase(2);
	}
};

struct HidingDerived : Base {
	Box getValue() const {
		Box box;
		box.value = 7;
		return box;
	}
};

int useInheritedStatic(Derived& d) {
	short s = 8;
	return d.shared_value + s;
}

int main() {
	Derived d;
	d.marker = 0;
	if (d.getValue().value != 42)
		return 1;
	if (d.compute() != 42)
		return 2;
	if (useInheritedStatic(d) != 50)
		return 3;

	HidingDerived hidden;
	if (hidden.getValue().value != 7)
		return 4;

	Base& base_ref = hidden;
	if (base_ref.getValue().value != 42)
		return 5;

	Base* base_ptr = &hidden;
	if (base_ptr->getValue().value != 42)
		return 6;

	return 0;
}
