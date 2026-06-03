struct NestedMemberCopyThisExample {
	int value;

	constexpr int sharedNested() {
		auto outer = [this]() mutable {
			auto inner = [&]() mutable {
				value += 2;
				return value;
			};
			return inner() + inner() + value;
		};
		return outer() + value;
	}

	constexpr int copiedNested() {
		auto outer = [*this]() mutable {
			auto inner = [&]() mutable {
				value += 2;
				return value;
			};
			return inner() + inner() + value;
		};
		return outer() + value;
	}
};

constexpr int sharedNested() {
	NestedMemberCopyThisExample example{40};
	return example.sharedNested();
}
constexpr int copiedNested() {
	NestedMemberCopyThisExample example{40};
	return example.copiedNested();
}
static_assert(sharedNested() == 174);
static_assert(copiedNested() == 170);

int main() {
	{
		NestedMemberCopyThisExample example{40};
		if (example.sharedNested() != 174) return 1;
	}
	{
		NestedMemberCopyThisExample example{40};
		if (example.copiedNested() != 170) return 2;
	}
	return 0;
}
