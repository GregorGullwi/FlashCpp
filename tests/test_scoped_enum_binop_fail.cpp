// C++20: scoped enum (enum class) does not allow implicit conversion in binary arithmetic.
// This should produce a compile error.
enum class Color { Red, Green, Blue };

int main() {
	Color c = Color::Green;
	int x = c + 1;  // ERROR: no implicit conversion from scoped enum in binary arithmetic
	return x;
}
