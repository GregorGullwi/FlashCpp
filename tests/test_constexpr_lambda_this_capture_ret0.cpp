struct CaptureThisExample {
	int x;

	constexpr int explicitThis() const {
		auto f = [this]() {
			return x + 2;
		};
		return f();
	}

	constexpr int copyThis() const {
		auto f = [*this]() {
			return x + 2;
		};
		return f();
	}
};

constexpr CaptureThisExample example{40};
static_assert(example.explicitThis() == 42);
static_assert(example.copyThis() == 42);

int main() {
	return 0;
}