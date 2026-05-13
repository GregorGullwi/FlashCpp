enum class Color : int {
	Red = 1,
	Blue = 42
};

using ColorAlias1 = Color;
using ColorAlias2 = ColorAlias1;

struct Paint {
	Color color;
	Paint(Color c) : color(c) {}
};

int main() {
	Paint paint{ColorAlias2(42)};
	return static_cast<int>(paint.color);
}
