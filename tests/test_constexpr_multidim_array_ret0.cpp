// Test: multi-dimensional array initialization and constexpr evaluation

// Global constexpr 2D array with full nested-brace init
constexpr int grid[2][3] = {{1,2,3},{4,5,6}};
static_assert(grid[0][0] == 1);
static_assert(grid[0][1] == 2);
static_assert(grid[0][2] == 3);
static_assert(grid[1][0] == 4);
static_assert(grid[1][1] == 5);
static_assert(grid[1][2] == 6);

// In a constexpr function: init + read
constexpr int mat_sum() {
    int mat[2][3] = {{1,2,3},{4,5,6}};
    int sum = 0;
    for (int i = 0; i < 2; i++)
        for (int j = 0; j < 3; j++)
            sum += mat[i][j];
    return sum;
}
static_assert(mat_sum() == 21);

// In a constexpr function: zero-init + subscript assignment
constexpr int mat_assign() {
    int mat[2][3] = {0};
    mat[0][1] = 5;
    mat[1][2] = 10;
    return mat[0][1] + mat[1][2];
}
static_assert(mat_assign() == 15);

int main() { return 0; }
