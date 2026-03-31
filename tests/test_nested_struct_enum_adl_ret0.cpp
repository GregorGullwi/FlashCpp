// ADL for enums nested in deeply-nested struct hierarchies.
// Per C++20 [basic.lookup.argdep]/2, the associated namespace of an
// enumeration type is its innermost enclosing *namespace* (not the
// enclosing class).  This test exercises multi-level struct nesting:
// namespace > struct > struct > struct > enum.
//
// Return value is 0 on success.
namespace ns {
struct A {
	struct B {
		struct C {
			enum class E { Lo,
						   Hi };
		};
	};
};

 // ADL should find this free function when called with an
 // ns::A::B::C::E argument — the associated namespace is "ns".
int eval(A::B::C::E e) {
	if (e == A::B::C::E::Lo)
		return 42;
	return 99;
}
} // namespace ns

int main() {
 // Unqualified call — ADL must search "ns" because that is the
 // innermost enclosing namespace of A::B::C::E.
	int r = eval(ns::A::B::C::E::Lo);
	if (r != 42)
		return 1;
	return 0;
}
