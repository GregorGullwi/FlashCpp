struct DefaultThisCaptureExample {
	int value;

	constexpr int read() const {
		return value + 1;
	}

	constexpr int byValueDefault() const {
		auto f = [=]() {
			return value + 1;
		};
		return f();
	}

	constexpr int byRefDefault() const {
		auto f = [&]() {
			return read();
		};
		return f();
	}
};

constexpr DefaultThisCaptureExample example{41};
static_assert(example.byValueDefault() == 42);
static_assert(example.byRefDefault() == 42);

int main() {
	return 0;
}