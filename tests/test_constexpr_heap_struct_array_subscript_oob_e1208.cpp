// Reduced regression for DiagnosticId::ConstantExpressionOutOfBoundsAccess (1208).
// C++20 [expr.const]/4, [basic.life]: indexing a constexpr heap array of
// structs outside its bounds is not permitted during constant evaluation.
struct Point {
	int x;
	int y;
};

constexpr int readHeapStructArray() {
	Point* pts = new Point[2]{{1, 2}, {3, 4}};
	return pts[7].x;
}

static_assert(readHeapStructArray() == 0);

int main() {
	return 0;
}
