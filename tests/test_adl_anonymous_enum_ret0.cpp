// Test: ADL with enums in anonymous namespaces
// C++20 [basic.lookup.argdep]/2: the associated namespace of an
// enumeration type is its innermost enclosing namespace.

namespace {

enum Color { Red, Green, Blue };

int color_value(Color c) {
	if (c == Red) return 10;
	if (c == Green) return 20;
	return 30;
}

} // anonymous namespace

int main() {
	Color c = Green;
	// ADL should find color_value via the enum's enclosing anonymous namespace
	int val = color_value(c);
	return val == 20 ? 0 : 1;
}
