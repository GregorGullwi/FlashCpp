// C++20: scoped enum (enum class) does not allow implicit conversion to int.
// This should produce a compile error.
enum class Color { Red, Green, Blue };

int main() {
	Color c = Color::Red;
	int x = c;  // ERROR: no implicit conversion from scoped enum to int
	return x;
}
