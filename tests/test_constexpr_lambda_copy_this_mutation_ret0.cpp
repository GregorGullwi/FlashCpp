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

constexpr int testCopyThisMutation() {
	CopyThisMutationExample example{40};
	return example.testCopyThisMutation();
}
static_assert(testCopyThisMutation() == 124);

int main() {
	CopyThisMutationExample example{40};
	if (example.testCopyThisMutation() != 124) return 1;
	return 0;
}
