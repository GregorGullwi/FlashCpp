// Test struct typedef alias operations
// This exercises the Type::UserDefined → IrType::Struct migration path.
// Using typedef/using aliases for struct types should work correctly
// through all codegen paths that now use isIrStructType(toIrType(...)).

struct Point {
	int x;
	int y;
};

using PointAlias = Point;

Point makePoint(int x, int y) {
	Point p;
	p.x = x;
	p.y = y;
	return p;
}

int main() {
	// Test typedef alias for struct
	PointAlias p1;
	p1.x = 10;
	p1.y = 20;
	if (p1.x != 10) return 1;
	if (p1.y != 20) return 2;

	// Test function returning struct used through alias
	PointAlias p2 = makePoint(3, 4);
	if (p2.x != 3) return 3;
	if (p2.y != 4) return 4;

	// Test struct assignment through alias
	PointAlias p3;
	p3 = p1;
	if (p3.x != 10) return 5;
	if (p3.y != 20) return 6;

	return 0;
}
