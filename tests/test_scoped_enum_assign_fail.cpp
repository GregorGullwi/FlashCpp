// C++20: scoped enum (enum class) does not allow implicit conversion in assignment.
// This should produce a compile error.
enum class Color { Red, Green, Blue };

int main() {
	Color c = Color::Green;
	int x = 0;
	x = c;  // ERROR: no implicit conversion from scoped enum to int in assignment
	return x;
}
