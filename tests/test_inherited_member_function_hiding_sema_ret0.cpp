// Test that the recursive sema lookup for inherited member functions respects
// derived-class name hiding instead of incorrectly walking into the base.

struct Box {
	int value;
};

struct Base {
	Box getValue() const {
		Box box;
		box.value = 1;
		return box;
	}
};

struct Derived : Base {
	Box getValue() const {
		Box box;
		box.value = 42;
		return box;
	}
};

int main() {
	Derived d;
	return d.getValue().value - 42;
}
