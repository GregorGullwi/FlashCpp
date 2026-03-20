// Regression test: multi-arg constructor where scoped enum appears at
// a non-first position. The diagnostic must check param[arg_idx], not params[0].

enum class Color { Red, Green, Blue };

struct Pixel {
	int x;
	Color color;
	Pixel(int xv, Color c) : x(xv), color(c) {}
};

int main() {
	Color c = Color::Green;
	Pixel p(10, c);  // Valid: second arg is Color, param[1] is Color — not an error
	if (p.x != 10) return 1;
	return 0;
}
