// C++20: scoped enum (enum class) does not allow implicit conversion in function arguments.
// This should produce a compile error.
enum class Color { Red,
				   Green,
				   Blue };

int identity(int x) { return x; }

int main() {
	Color c = Color::Blue;
	return identity(c);	// ERROR: no implicit conversion from scoped enum to int in function argument
}
