// Array-of-struct and array-of-enum extents must import from the published
// entity on the type index. Perturbation: publishedNominalEntity ignores that
// index and returns only an entity already stored on the specifier, so
// requireCanonicalShape throws while lowering these declarations.
struct Point {
	int x;
	int y;
};

enum Color { Red = 1, Blue = 2, Green = 3 };

int main() {
	Point pts[2];
	pts[0].x = 3;
	pts[1].x = 4;
	Color row[2][2] = {{Red, Blue}, {Green, Red}};
	Point grid[2][2];
	grid[0][0].y = 1;
	grid[1][1].y = 2;
	Point (*pointer)[2] = &pts;
	int sum = pts[0].x + pts[1].x + static_cast<int>(row[0][1]) + grid[0][0].y + grid[1][1].y;
	if ((*pointer)[0].x != 3) {
		return 0;
	}
	return sum;
}
