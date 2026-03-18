// ADL with enum-typed arguments.
// Per C++20 [basic.lookup.argdep]/2, the associated namespaces of an
// enumeration type include the innermost enclosing namespace of its
// declaration.  A free function declared in that namespace should be
// findable via ADL when called with an argument of that enum type.
//
// Return value is 0 on success.
namespace ns {
	enum class Color { Red, Green, Blue };

	// Regular free function in namespace ns — ADL should find it when
	// called with an ns::Color argument from outside the namespace.
	int color_index(Color c) {
		if (c == Color::Red) return 0;
		if (c == Color::Green) return 1;
		return 2;
	}
}

int main() {
	// ADL should find ns::color_index because the argument is ns::Color,
	// whose associated namespace is "ns".
	return color_index(ns::Color::Red);  // Expected: 0
}
