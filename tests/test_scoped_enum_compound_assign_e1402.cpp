// C++20: scoped enum (enum class) does not allow implicit conversion in compound assignment.
// This should produce a compile error.
enum class Color { Red,
				   Green,
				   Blue };

int main() {
	Color c = Color::Red;
	int x = 10;
	x += c;	// ERROR: no implicit conversion from scoped enum to int in compound assignment
	return x;
}
