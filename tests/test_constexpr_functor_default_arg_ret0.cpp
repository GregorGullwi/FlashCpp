struct Add {
	constexpr Add(int) {}

	constexpr int operator()(int a, int b = 2) const {
		return a + b;
	}
};

constexpr Add add(0);
constexpr int result = add(40);

static_assert(result == 42);

int main() {
	return result - 42;
}
