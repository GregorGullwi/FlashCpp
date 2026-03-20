enum class Color {
	Red = 1,
	Green = 2
};

struct Pixel {
	Color color;
	int alpha;

	Pixel(Color c, int a = 255) : color(c), alpha(a) {}
	Pixel(int value) : color(Color::Red), alpha(value) {}
};

int main() {
	Color c = Color::Green;
	Pixel pixel(c);
	return pixel.alpha == 255 ? 0 : 1;
}
