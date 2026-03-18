// ADL with enum-typed arguments.
// Per C++20 [basic.lookup.argdep]/2, the associated namespaces of an
// enumeration type include the innermost enclosing namespace of its
// declaration.  A free function declared in that namespace should be
// findable via ADL when called with an argument of that enum type.
//
// This test covers three scenarios:
//   1. Enum directly in a named namespace (basic case)
//   2. Enum declared inside a class body (associated namespace is the
//      enclosing namespace of the class, not the class itself)
//   3. Enum in a nested namespace
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

	// Scenario 2: Enum declared inside a class body.
	// Per C++20 [basic.lookup.argdep]/2, the associated namespace of
	// Container::Status is "ns" (the innermost enclosing namespace),
	// not Container itself.
	struct Container {
		enum class Status { Ok, Error };
	};

	int check_status(Container::Status s) {
		if (s == Container::Status::Ok) return 10;
		return 20;
	}
}

namespace outer {
	namespace inner {
		enum class Level { Low, High };
	}
	// Scenario 3: Function in outer namespace, enum in outer::inner.
	// ADL for inner::Level should find outer::inner as the associated
	// namespace, not outer. This function should NOT be found via ADL
	// for an inner::Level argument (it's in the wrong namespace).
	// We test the positive case: a function in inner.
}

namespace outer {
	namespace inner {
		int level_value(Level l) {
			if (l == Level::Low) return 100;
			return 200;
		}
	}
}

int main() {
	// Scenario 1: basic enum in namespace
	// ADL should find ns::color_index because the argument is ns::Color,
	// whose associated namespace is "ns".
	int r1 = color_index(ns::Color::Red);  // Expected: 0
	if (r1 != 0) return 1;

	// Scenario 2: enum inside a class body
	// ADL should find ns::check_status because ns::Container::Status's
	// associated namespace is "ns".
	int r2 = check_status(ns::Container::Status::Ok);  // Expected: 10
	if (r2 != 10) return 2;

	// Scenario 3: enum in nested namespace
	// ADL should find outer::inner::level_value because
	// outer::inner::Level's associated namespace is "outer::inner".
	int r3 = level_value(outer::inner::Level::Low);  // Expected: 100
	if (r3 != 100) return 3;

	return 0;
}
