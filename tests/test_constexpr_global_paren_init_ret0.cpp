// Test: constexpr global struct initialized with paren-init syntax at global scope.
// Previously broken: parser created InitializerListNode (not ConstructorCallNode)
// for Type::UserDefined structs, and ConstructorCallNode was not wrapped in
// ExpressionNode, so IR codegen never emitted init data.
struct Point {
	int x;
	int y;
	constexpr Point(int a, int b) : x(a), y(b) {}
};

struct RGB {
	unsigned char r;
	unsigned char g;
	unsigned char b;
	constexpr RGB(unsigned char rr, unsigned char gg, unsigned char bb) : r(rr), g(gg), b(bb) {}
};

constexpr Point gp(10, 20);
constexpr RGB gcol(255, 128, 0);

static_assert(gp.x == 10);
static_assert(gp.y == 20);
static_assert(gcol.r == 255);
static_assert(gcol.g == 128);
static_assert(gcol.b == 0);

int main() {
	if (gp.x != 10) return 1;
	if (gp.y != 20) return 2;
	if (gcol.r != 255) return 3;
	if (gcol.g != 128) return 4;
	if (gcol.b != 0) return 5;
	return 0;
}
