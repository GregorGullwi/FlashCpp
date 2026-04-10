template <typename T>
constexpr T add(T a, T b = 2) {
	return a + b;
}

constexpr int compute() {
	return add<int>(40);
}

static_assert(compute() == 42);

int main() {
	return compute() - 42;
}
