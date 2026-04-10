template <typename T>
constexpr T addWithDefault(T a, T offset = 2) {
	return a + offset;
}

constexpr int compute() {
	return addWithDefault<int>(40);
}

static_assert(compute() == 42);

int main() {
	return compute() - 42;
}
