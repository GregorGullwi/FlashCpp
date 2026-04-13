enum Color { Red = 10, Green = 20, Blue = 12 };

namespace config {
Color selected = Blue;
}

int main() {
	// Exercises emitQualifiedGlobalLoad with an enum-typed global variable.
	// The loaded value must be lowered to its underlying integer type so
	// arithmetic produces correct results (12 + 30 = 42).
	return config::selected + 30;
}
