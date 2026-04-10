constexpr int add(int a, int b = 2) {
	return a + b;
}

constexpr int compute() {
	return add(40);
}

static_assert(compute() == 42);

int main() {
	return compute() - 42;
}
