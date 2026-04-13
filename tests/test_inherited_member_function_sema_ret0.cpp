// Test that sema can infer the type of an inherited non-static member function
// accessed through a derived-class object. This exercises the sema-owned
// MemberAccessNode typing path before call-argument conversion annotation.

struct Base {
	short value;

	short getValue() const { return value; }
};

struct Derived : Base {
	int marker;
};

int takeInt(int value) {
	return value + 1;
}

int main() {
	Derived d;
	d.value = 41;
	d.marker = 0;
	int result = takeInt(d.getValue());
	return result - 42;
}
