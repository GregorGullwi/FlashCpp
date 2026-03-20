// Test: scoped enum operands with bitwise operators should be diagnosed as ill-formed
// C++20 [expr.bit.and], [expr.bit.or], [expr.bit.xor]: scoped enums do not
// implicitly convert to their underlying type for bitwise operations.
enum class Color { Red = 1, Green = 2, Blue = 4 };

int main() {
	Color a = Color::Red;
	Color b = Color::Green;
	int c = a | b;  // ill-formed: scoped enum in bitwise op
	return c;
}
