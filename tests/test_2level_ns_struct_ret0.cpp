// Test 2-level struct nesting within a namespace.
// Before fix: "A::B" was registered without "ns::" prefix, so ns::A::B was unresolvable.
// Also exercises unqualified access (A::B) from within the namespace.
namespace ns {
struct A {
	struct B {
		int value;
	};
};
int make_b_value() {
	// Unqualified access from within namespace ns: A::B (no ns:: prefix)
	A::B b;
	b.value = 42;
	return b.value;
}
} // namespace ns
int main() {
 // Fully qualified access from outside namespace
	ns::A::B x;
	x.value = 7;
	return ns::make_b_value() - x.value - 35;
}
