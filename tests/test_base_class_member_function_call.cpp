// Test: Base class member function calls from derived class member functions should receive implicit 'this'

struct Base {
	int value;

	int getValue() {
		return value;
	}

	void setValue(int v) {
		value = v;
	}
};

struct Derived : Base {
	int multiplier;

	int compute() {
		setValue(10);		  // Call base class member function - should pass 'this'
		return getValue() * multiplier;	// Call base class member function - should pass 'this'
	}
};

int main() {
	Derived d;
	d.multiplier = 5;
	int result = d.compute();
	if (result != 50) return 1;  // Expected 10 * 5 = 50
	return 0;
}
