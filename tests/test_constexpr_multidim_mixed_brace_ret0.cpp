constexpr int global_mixed_tail[2][3] = {1, 2, 3, {4, 5, 6}};
static_assert(global_mixed_tail[0][0] == 1);
static_assert(global_mixed_tail[0][1] == 2);
static_assert(global_mixed_tail[0][2] == 3);
static_assert(global_mixed_tail[1][0] == 4);
static_assert(global_mixed_tail[1][1] == 5);
static_assert(global_mixed_tail[1][2] == 6);

constexpr int mixed_tail_brace() {
	int mat[2][3] = {1, 2, 3, {4, 5, 6}};
	return mat[0][0] + mat[0][1] + mat[0][2] +
		   mat[1][0] * 10 + mat[1][1] * 10 + mat[1][2] * 10;
}
static_assert(mixed_tail_brace() == 156);

constexpr int mixed_head_brace() {
	int mat[2][3] = {{1, 2, 3}, 4, 5, 6};
	return mat[0][0] + mat[0][1] + mat[0][2] +
		   mat[1][0] * 10 + mat[1][1] * 10 + mat[1][2] * 10;
}
static_assert(mixed_head_brace() == 156);

int main() {
	return 0;
}
