// Test: fully-flattened brace-elision for multi-dimensional arrays per C++20 dcl.init.aggr rules.
// int[M][N] = {s1, s2, ...} distributes scalars sequentially across inner dimensions.

// Full fill: 6 scalars → 2x3 array
constexpr int mat_full_flat() {
	int mat[2][3] = {1, 2, 3, 4, 5, 6};
	return mat[0][0] + mat[0][1] + mat[0][2] + mat[1][0] + mat[1][1] + mat[1][2];
}
static_assert(mat_full_flat() == 21);

// Partial fill: 2 scalars → first row gets {1,2,0}, second row gets {0,0,0}
constexpr int mat_partial_flat() {
	int mat[2][3] = {1, 2};
	return mat[0][0] * 100 + mat[0][1] * 10 + mat[0][2];
}
static_assert(mat_partial_flat() == 120);

// Verify second row is zero when partial fill
constexpr int mat_partial_row2() {
	int mat[2][3] = {1, 2};
	return mat[1][0] + mat[1][1] + mat[1][2];
}
static_assert(mat_partial_row2() == 0);

// Exact row fill: 3 scalars → first row {7,8,9}, second row {0,0,0}
constexpr int mat_one_row_flat() {
	int mat[2][3] = {7, 8, 9};
	return mat[0][0] * 100 + mat[0][1] * 10 + mat[0][2];
}
static_assert(mat_one_row_flat() == 789);

constexpr int mat_one_row_second_zero() {
	int mat[2][3] = {7, 8, 9};
	return mat[1][0] + mat[1][1] + mat[1][2];
}
static_assert(mat_one_row_second_zero() == 0);

int main() {
	return 0;
}
