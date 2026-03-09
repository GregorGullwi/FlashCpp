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

constexpr ThisSharedMutationExample example{40};
static_assert(example.testThisMutation() == 123);

int main() {
	return 0;
}