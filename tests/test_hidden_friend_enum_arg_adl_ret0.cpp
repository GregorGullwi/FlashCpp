// Hidden friend function that accepts both a struct and an enum argument.
// ADL finds the hidden friend via the struct-typed argument (ns::Palette),
// and the enum argument (ns::Color) is also resolved correctly.
//
// This test combines:
//   1. Hidden friend functions (only visible via ADL, not ordinary lookup)
//   2. Enum types as function arguments alongside struct types
//
// Return value is 0 on success.
namespace ns {
enum class Color { Red,
				   Green,
				   Blue };

struct Palette {
	// Hidden friend: only findable via ADL when a Palette argument is provided.
	// Also takes an ns::Color enum argument.
	friend int color_index(Palette, Color c) {
		if (c == Color::Red)
			return 0;
		if (c == Color::Green)
			return 1;
		return 2;
	}
};
} // namespace ns

int main() {
	ns::Palette p;
 // ADL finds ns::color_index because p is of type ns::Palette,
 // whose associated namespace is "ns".
	return color_index(p, ns::Color::Red);  // Expected: 0
}
