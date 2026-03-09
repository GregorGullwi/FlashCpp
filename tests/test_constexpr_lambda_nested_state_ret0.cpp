constexpr int nested_outer_closure_state() {
	auto outer = [x = 40]() mutable {
		auto inner = [&]() mutable {
			x += 2;
			return x;
		};
		return inner() + x;
	};
	return outer() + outer();
}

struct NestedMemberLambdaStateExample {
	int value;

	constexpr int run() {
		auto outer = [this]() mutable {
			auto inner = [this]() mutable {
				value += 2;
				return value;
			};
			return inner() + value;
		};
		return outer() + outer();
	}
};

constexpr NestedMemberLambdaStateExample example{40};
static_assert(nested_outer_closure_state() == 172);
static_assert(example.run() == 172);

int main() {
	return 0;
}