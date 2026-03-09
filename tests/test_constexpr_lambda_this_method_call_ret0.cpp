struct CaptureMethodExample {
	int x;

	constexpr int read() const {
		return x + 2;
	}

	constexpr int explicitThisMethod() const {
		auto f = [this]() {
			return this->read();
		};
		return f();
	}

	constexpr int copyThisMethod() const {
		auto f = [*this]() {
			return this->read();
		};
		return f();
	}
};

constexpr CaptureMethodExample example{40};
static_assert(example.explicitThisMethod() == 42);
static_assert(example.copyThisMethod() == 42);

int main() {
	return 0;
}