// Test 2-level struct nesting within a namespace.
// Before fix: "A::B" was registered without "ns::" prefix, so ns::A::B was unresolvable.
namespace ns {
	struct A {
		struct B {
			int value;
		};
	};
	int make_b_value() {
		ns::A::B b;
		b.value = 42;
		return b.value;
	}
}
int main() {
	ns::A::B x;
	x.value = 7;
	return ns::make_b_value() - x.value - 35;
}
