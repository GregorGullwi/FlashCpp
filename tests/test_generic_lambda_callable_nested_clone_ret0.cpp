struct Adder {
	int bias;

	constexpr int operator()(int value) const {
		return bias + value;
	}
};

int main() {
	auto outer = []<typename Callable>(Callable callable, int value) {
		auto inner = [=]() {
			return callable(value);
		};
		return inner();
	};

	return outer(Adder{2}, 40) == 42 ? 0 : 1;
}
