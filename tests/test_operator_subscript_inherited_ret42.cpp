// Test operator[] inherited from a base class — sema resolves through base classes.
// Verifies that tryResolveSubscriptOperator walks base_classes to find operator[].

struct BaseContainer {
	int data[4];

	int operator[](int index) {
		return data[index];
	}
};

struct DerivedContainer : BaseContainer {
	// No operator[] defined here — should inherit from BaseContainer.
};

int main() {
	DerivedContainer d;
	d.data[0] = 10;
	d.data[1] = 32;
	d.data[2] = 99;
	// d[0] + d[1] = 10 + 32 = 42
	return d[0] + d[1];
}
