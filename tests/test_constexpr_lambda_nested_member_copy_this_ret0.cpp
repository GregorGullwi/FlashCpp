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

constexpr NestedMemberCopyThisExample example{40};
static_assert(example.sharedNested() == 174);
static_assert(example.copiedNested() == 170);

int main() {
	return 0;
}