struct ConstructorBodyMemberDependencyExample {
	int x;
	int y;

	constexpr ConstructorBodyMemberDependencyExample(int input) {
		x = input;
		y = x + 2;
	}
};

constexpr ConstructorBodyMemberDependencyExample example{40};
static_assert(example.x == 40);
static_assert(example.y == 42);

int main() {
	return 0;
}