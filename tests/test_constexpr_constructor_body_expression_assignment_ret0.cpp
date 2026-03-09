struct ConstructorBodyExpressionAssignmentExample {
	int value;

	constexpr ConstructorBodyExpressionAssignmentExample(int input) {
		value = input + 2;
	}
};

constexpr ConstructorBodyExpressionAssignmentExample example{40};
static_assert(example.value == 42);

int main() {
	return 0;
}
