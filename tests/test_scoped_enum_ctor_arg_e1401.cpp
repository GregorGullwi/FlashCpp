// Phase 17: Verify scoped enum constructor argument diagnostic.
// Scoped enums cannot be implicitly converted in constructor arguments.

enum class Color { Red,
				   Green,
				   Blue };

struct Pixel {
	int value;
	Pixel(int v) : value(v) {}
};

int main() {
	Color c = Color::Red;
	Pixel p(c);	// Error: scoped enum → int in constructor argument
	return p.value;
}
