// Phase 17: Verify that a constructor accepting the same scoped enum type
// compiles and runs correctly. This is valid C++ — the scoped enum diagnostic
// must NOT fire when the constructor parameter matches the argument type.

enum class Color { Red,
				   Green,
				   Blue };

struct Pixel {
	Color color;
	Pixel(Color c) : color(c) {}
};

int main() {
	Color c = Color::Green;
	Pixel p(c);	// Valid: constructor accepts Color, argument is Color
	if (static_cast<int>(p.color) != 1)
		return 1;

	Pixel p2(Color::Blue);  // Valid: direct scoped enum literal
	if (static_cast<int>(p2.color) != 2)
		return 2;

	return 0;
}
