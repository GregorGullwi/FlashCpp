// Test that sema can recover an inherited non-static member function through a
// derived-class receiver and keep the resulting call expression strongly typed
// enough for a follow-on member access.

struct Box {
	int value;
};

struct Base {
	Box getValue() const {
		Box box;
		box.value = 42;
		return box;
	}
};

struct Derived : Base {
	int marker;
};

int main() {
	Derived d;
	d.marker = 0;
	return d.getValue().value - 42;
}
