// Test 3-level struct nesting within a namespace.
// Before fix: type was registered as "B::C" (back() only), making ns::A::B::C unresolvable.
namespace ns {
	struct A {
		struct B {
			struct C {
				int value;
			};
		};
	};
}
int main() {
	ns::A::B::C c;
	c.value = 0;
	return c.value;
}
