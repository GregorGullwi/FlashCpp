struct Box {
	int a;
	int b;

	constexpr int sum() const {
		return a + b;
	}
};

constexpr Box makeBox(int a, int b) {
	return Box{a, b};
}

constexpr int sumViaConditional() {
	Box x{7, 8};
	return (true ? x : Box{1, 2}).sum();
}

constexpr int sumViaNestedConditional(bool pick_first) {
	Box first{3, 4};
	Box second{9, 1};
	return (pick_first ? first : second).sum();
}

static_assert(sumViaConditional() == 15);
static_assert(sumViaNestedConditional(true) == 7);
static_assert(sumViaNestedConditional(false) == 10);
static_assert((true ? makeBox(5, 6) : Box{0, 0}).sum() == 11);

int main() {
	return sumViaConditional() == 15 &&
				   sumViaNestedConditional(true) == 7 &&
				   sumViaNestedConditional(false) == 10 &&
				   (true ? makeBox(5, 6) : Box{0, 0}).sum() == 11
			   ? 0
			   : 1;
}
