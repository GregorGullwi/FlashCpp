template <typename T>
struct ReceiverSelector {
	constexpr ReceiverSelector() = default;

	constexpr int selected() {
		return value();
	}

	constexpr int selected() const {
		return value();
	}

	constexpr int value() {
		return 1;
	}

	constexpr int value() const {
		return 2;
	}
};

static_assert(ReceiverSelector<int>{}.selected() == 1);

constexpr ReceiverSelector<int> const_selector{};
static_assert(const_selector.selected() == 2);

int main() {
	ReceiverSelector<int> selector;
	if (selector.selected() != 1) {
		return 1;
	}

	const ReceiverSelector<int> const_runtime_selector{};
	if (const_runtime_selector.selected() != 2) {
		return 2;
	}

	return 0;
}
