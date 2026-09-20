// Regression: local aggregate initialization with nested braces over
// multidimensional arrays of structs must store every element at its row-major
// offset. The old local lowering treated each outer brace group as a single
// struct initializer, so only the first element's members were touched (with
// the wrong mapping) and the remaining elements stayed zeroed.

struct Point2D {
	int x;
	int y;
};

struct Mixed {
	int tag;
	Point2D origin;
	int count;
};

int main() {
	Point2D pts[2][2] = {{{1, 2}, {3, 4}}, {{5, 6}, {7, 8}}};
	if (pts[0][0].x != 1 || pts[0][0].y != 2) return 1;
	if (pts[0][1].x != 3 || pts[0][1].y != 4) return 2;
	if (pts[1][0].x != 5 || pts[1][0].y != 6) return 3;
	if (pts[1][1].x != 7 || pts[1][1].y != 8) return 4;

	Point2D cube[2][1][2] = {
		{{{10, 11}, {12, 13}}},
		{{{14, 15}, {16, 17}}},
	};
	if (cube[0][0][0].x != 10 || cube[0][0][0].y != 11) return 5;
	if (cube[0][0][1].x != 12 || cube[0][0][1].y != 13) return 6;
	if (cube[1][0][0].x != 14 || cube[1][0][0].y != 15) return 7;
	if (cube[1][0][1].x != 16 || cube[1][0][1].y != 17) return 8;

	Mixed mixed[2][2] = {
		{{21, {22, 23}, 24}, {25, {26, 27}, 28}},
		{{29, {30, 31}, 32}, {33, {34, 35}, 36}},
	};
	if (mixed[0][0].tag != 21 || mixed[0][0].origin.x != 22 || mixed[0][0].origin.y != 23 || mixed[0][0].count != 24) return 9;
	if (mixed[0][1].tag != 25 || mixed[0][1].origin.x != 26 || mixed[0][1].origin.y != 27 || mixed[0][1].count != 28) return 10;
	if (mixed[1][0].tag != 29 || mixed[1][0].origin.x != 30 || mixed[1][0].origin.y != 31 || mixed[1][0].count != 32) return 11;
	if (mixed[1][1].tag != 33 || mixed[1][1].origin.x != 34 || mixed[1][1].origin.y != 35 || mixed[1][1].count != 36) return 12;

	Point2D a = {1, 2};
	Point2D b = {3, 4};
	Point2D c = {5, 6};
	Point2D d = {7, 8};
	Point2D from_exprs[2][2] = {{a, b}, {c, d}};
	if (from_exprs[0][0].x != 1 || from_exprs[0][0].y != 2) return 13;
	if (from_exprs[0][1].x != 3 || from_exprs[0][1].y != 4) return 14;
	if (from_exprs[1][0].x != 5 || from_exprs[1][0].y != 6) return 15;
	if (from_exprs[1][1].x != 7 || from_exprs[1][1].y != 8) return 16;

	int m[2][3] = {{1, 2, 3}, {4, 5, 6}};
	if (m[0][0] != 1 || m[0][1] != 2 || m[0][2] != 3) return 17;
	if (m[1][0] != 4 || m[1][1] != 5 || m[1][2] != 6) return 18;

	return 42;
}
