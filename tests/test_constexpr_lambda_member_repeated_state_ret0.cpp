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

constexpr int sharedThisState() {
	MemberRepeatedStateExample example{40};
	return example.sharedThisState();
}
constexpr int copiedThisState() {
	MemberRepeatedStateExample example{40};
	return example.copiedThisState();
}
static_assert(sharedThisState() == 130);
static_assert(copiedThisState() == 126);

int main() {
	{
		MemberRepeatedStateExample example{40};
		if (example.sharedThisState() != 130) return 1;
	}
	{
		MemberRepeatedStateExample example{40};
		if (example.copiedThisState() != 126) return 2;
	}
	return 0;
}
