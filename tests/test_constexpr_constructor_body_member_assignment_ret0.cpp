struct ConstructorBodyAssignmentExample {
	int value;

	constexpr ConstructorBodyAssignmentExample(int input) {
		value = input;
	}
};

constexpr ConstructorBodyAssignmentExample example{42};
static_assert(example.value == 42);

int main() {
	return 0;
}