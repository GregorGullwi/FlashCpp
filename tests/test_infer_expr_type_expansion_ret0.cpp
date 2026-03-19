// Test inferExpressionType expansion: ConstructorCallNode, StringLiteralNode,
// noexcept, offsetof.
// Verifies these expression types are properly inferred by sema for annotation.

struct Point {
	int x;
	int y;
};

long long getOffset() {
	return offsetof(Point, y);  // offsetof returns size_t → implicit conv to long long
}

bool checkNoexcept() {
	int x = 42;
	return noexcept(x + 1);  // noexcept returns bool
}

int main() {
	// ConstructorCallNode: Point{1, 2} returns Point (struct type)
	Point p = Point{1, 2};
	if (p.x != 1) return 1;
	if (p.y != 2) return 2;

	// offsetof type inference
	long long off = getOffset();
	if (off != 4) return 3;

	// noexcept type inference
	bool ne = checkNoexcept();
	if (!ne) return 4;

	return 0;
}
