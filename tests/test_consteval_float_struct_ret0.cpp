// Test consteval function returning a struct with float members.
// Guards against the bug where float/double member values would be truncated to int
// instead of preserving their IEEE 754 bit patterns.

struct Vec2f { float x, y; };
consteval Vec2f make_vec2f(float x, float y) { return {x, y}; }

int main() {
	// consteval call is folded at compile time; the resulting Vec2f must have
	// the exact float values 1.5f and 2.5f (not truncated to 1 and 2).
	Vec2f v = make_vec2f(1.5f, 2.5f);
	// 1.5f + 2.5f == 4.0f exactly in IEEE 754; cast to int is 4.
	return static_cast<int>(v.x + v.y) - 4; // 0
}
