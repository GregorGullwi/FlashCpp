// Test: non-constexpr global multidimensional array initialization and runtime reads.
// Exercises the fallback flatten-and-append path in global init-data emission
// when the constexpr materializer is not used (no constexpr keyword).

// Basic 2D array with nested-brace init (no constexpr)
int grid[2][3] = {{1, 2, 3}, {4, 5, 6}};

// Partial inner init: inner braces shorter than inner dimension, rest zero-filled
int sparse[2][3] = {{10}, {20}};

// 3D array without constexpr
int cube[2][2][3] = {{{1,2,3},{4,5,6}}, {{7,8,9},{10,11,12}}};

// Single-row 2D array
int single[1][4] = {{100, 200, 300, 400}};

int main() {
	// Verify grid values
	if (grid[0][0] != 1 || grid[0][1] != 2 || grid[0][2] != 3)
		return 1;
	if (grid[1][0] != 4 || grid[1][1] != 5 || grid[1][2] != 6)
		return 2;

	// Verify sparse: partial init with zero-fill
	if (sparse[0][0] != 10 || sparse[0][1] != 0 || sparse[0][2] != 0)
		return 3;
	if (sparse[1][0] != 20 || sparse[1][1] != 0 || sparse[1][2] != 0)
		return 4;

	// Verify 3D cube
	if (cube[0][0][0] != 1 || cube[0][0][2] != 3 || cube[0][1][0] != 4 || cube[0][1][2] != 6)
		return 5;
	if (cube[1][0][0] != 7 || cube[1][0][2] != 9 || cube[1][1][0] != 10 || cube[1][1][2] != 12)
		return 6;

	// Verify single-row 2D
	if (single[0][0] != 100 || single[0][1] != 200 || single[0][2] != 300 || single[0][3] != 400)
		return 7;

	return 0;
}
