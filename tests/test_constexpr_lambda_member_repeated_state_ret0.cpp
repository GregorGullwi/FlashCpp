struct MemberRepeatedStateExample {
	int value;

	constexpr int sharedThisState() {
		auto f = [this]() mutable {
			value += 2;
			return value;
		};
		return f() + f() + value;
	}

	constexpr int copiedThisState() {
		auto f = [*this]() mutable {
			value += 2;
			return value;
		};
		return f() + f() + value;
	}
};

constexpr MemberRepeatedStateExample example{40};
static_assert(example.sharedThisState() == 130);
static_assert(example.copiedThisState() == 126);

int main() {
	return 0;
}