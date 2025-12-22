struct Counter {
	int value = 10;

	int run() {
		auto first = [*this]() mutable {
			value += 1;
			value *= 2;
			return value;  // (10 + 1) * 2 = 22
		};

		auto second = [*this]() mutable {
			value += 3;
			value *= 4;
			return value;  // (10 + 3) * 4 = 52
		};

		int first_result = first();
		int second_result = second();

		// Original value should remain unchanged for [*this] captures
		return value + first_result + second_result;  // 10 + 22 + 52 = 84
	}
};

int main() {
	Counter c;
	return c.run();
}
