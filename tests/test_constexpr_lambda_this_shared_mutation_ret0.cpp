struct ThisSharedMutationExample {
	int value;

	constexpr int bump() {
		value += 1;
		return value;
	}

	constexpr int testThisMutation() {
		auto f = [this]() mutable {
			return this->bump() + value;
		};
		return f() + value;
	}
};

constexpr int testThisMutation() {
	ThisSharedMutationExample example{40};
	return example.testThisMutation();
}
static_assert(testThisMutation() == 123);

int main() {
	ThisSharedMutationExample example{40};
	if (example.testThisMutation() != 123) return 1;
	return 0;
}
