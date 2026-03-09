struct CopyThisMutationExample {
	int value;

	constexpr int bump() {
		value += 2;
		return value;
	}

	constexpr int testCopyThisMutation() {
		auto f = [*this]() mutable {
			return this->bump() + value;
		};
		return f() + value;
	}
};

constexpr CopyThisMutationExample example{40};
static_assert(example.testCopyThisMutation() == 124);

int main() {
	return 0;
}