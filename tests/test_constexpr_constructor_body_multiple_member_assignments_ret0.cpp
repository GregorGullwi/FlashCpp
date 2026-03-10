struct ConstructorBodyMultipleAssignmentsExample {
	int x;
	int y;

	constexpr ConstructorBodyMultipleAssignmentsExample(int input_x, int input_y) {
		x = input_x;
		y = input_y;
	}
};

constexpr ConstructorBodyMultipleAssignmentsExample example{40, 2};
static_assert(example.x == 40);
static_assert(example.y == 2);

int main() {
	return 0;
}