// Test: constructor overload resolution with default arguments
// The only viable constructor has a default parameter.
// Pixel(Color c, int alpha = 255) called with 1 arg.
// Previously broken in sema when parser_.get_expression_type() returned
// nullopt for scoped enum arguments, preventing resolve_constructor_overload
// from running and leaving default arg handling untested.

enum class Color { Red = 1,
				   Green = 2,
				   Blue = 3 };

struct Pixel {
	int color_val;
	int alpha;
	Pixel(Color c, int alpha = 255) : color_val(static_cast<int>(c)), alpha(alpha) {}
};

int main() {
	Pixel p(Color::Green);  // uses default alpha = 255
 // color_val == 2, alpha == 255 => 2 + 255 - 257 == 0
	return p.color_val + p.alpha - 257;
}
