// Test that types declared in a namespace are properly tracked
// with namespace context, including functions and struct members.

namespace geom {
struct Point {
	int x;
	int y;
	int sum() const { return x + y; }
};

int get_sum(Point p) {
	return p.sum();
}
} // namespace geom

int main() {
	geom::Point p{20, 22};
	return geom::get_sum(p); // 42
}
