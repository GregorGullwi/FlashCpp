// Test: 3D array assignment and reads in constexpr evaluation

constexpr int cube_assign() {
	int cube[2][2][2] = {0};
	cube[0][1][1] = 5;
	cube[1][0][1] = 7;
	cube[1][1][0] = cube[0][1][1] + cube[1][0][1];
	return cube[0][0][0] + cube[0][1][1] + cube[1][0][1] + cube[1][1][0];
}
static_assert(cube_assign() == 24);

constexpr int cube_loop_sum() {
	int cube[2][2][2] = {0};
	for (int i = 0; i < 2; i++)
		for (int j = 0; j < 2; j++)
			for (int k = 0; k < 2; k++)
				cube[i][j][k] = i + j + k;

	int sum = 0;
	for (int i = 0; i < 2; i++)
		for (int j = 0; j < 2; j++)
			for (int k = 0; k < 2; k++)
				sum += cube[i][j][k];
	return sum;
}
static_assert(cube_loop_sum() == 12);

int main() { return 0; }
