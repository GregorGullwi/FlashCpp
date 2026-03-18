// Test 3-level struct nesting within a namespace.
// Before fix: type was registered as "B::C" (back() only), making ns::A::B::C unresolvable.
// Also exercises unqualified access (A::B::C) from within the namespace.
namespace ns {
	struct A {
		struct B {
			struct C {
				int value;
			};
		};
	};
	int make_c_value() {
		// Unqualified access from within namespace ns: A::B::C (no ns:: prefix)
		A::B::C c;
		c.value = 10;
		return c.value;
	}
}
int main() {
	// Fully qualified access from outside namespace
	ns::A::B::C c;
	c.value = 0;
	// Verify both access patterns work and produce correct values
	return c.value + ns::make_c_value() - 10;
}
