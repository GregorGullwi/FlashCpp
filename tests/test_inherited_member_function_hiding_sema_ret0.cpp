// Test that the recursive sema lookup for inherited member functions respects
// derived-class name hiding instead of incorrectly walking into the base.
// Also verifies that calling through a Base& or Base* resolves to the base
// method (static type determines non-virtual lookup).

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

	// Call through Derived – name hiding means Derived::getValue() returns 42.
	int derived_val = d.getValue().value;
	if (derived_val != 42)
		return 1;

	// Call through a Base reference – should resolve to Base::getValue() returning 1.
	Base& base_ref = d;
	int base_ref_val = base_ref.getValue().value;
	if (base_ref_val != 1)
		return 2;

	// Call through a Base pointer – same expectation.
	Base* base_ptr = &d;
	int base_ptr_val = base_ptr->getValue().value;
	if (base_ptr_val != 1)
		return 3;

	return 0;
}
