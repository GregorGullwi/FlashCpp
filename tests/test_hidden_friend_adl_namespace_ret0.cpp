// Hidden friend function inside a namespace, called with a namespaced argument.
// The call is valid because ADL: s is of type ns::S, so the associated
// namespace is "ns" which contains the hidden friend get_value.
// This tests that ADL correctly resolves the struct's namespace_handle
// when the struct is not in the global namespace.
namespace ns {
	struct S {
		int x;
		friend int get_value(S& s) { return s.x; }
	};
}
int main() {
	ns::S s;
	s.x = 42;
	return get_value(s) - 42;  // ADL finds ns::get_value; 42 - 42 == 0
}
