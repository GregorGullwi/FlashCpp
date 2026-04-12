struct AddOne {
	constexpr AddOne() {}

	constexpr int operator()(int value) const {
		return value + 1;
	}

	constexpr int operator()(long value) const {
		return static_cast<int>(value) + 2;
	}
};

constexpr AddOne global_add{};
constexpr int global_result = global_add(40);
static_assert(global_result == 41);

int main() {
	if (global_result != 41)
		return 1;
	if (global_add(40) != 41)
		return 2;
	return 0;
}
