struct Counter {
	constexpr int add(int a, int b = 2) const {
		return a + b;
	}
};

constexpr int compute() {
	Counter counter{};
	return counter.add(40);
}

static_assert(compute() == 42);

int main() {
	return compute() - 42;
}
