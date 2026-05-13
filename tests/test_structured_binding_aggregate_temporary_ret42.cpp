// Structured binding should work for temporary aggregate objects as well.
// Expected return: 42

struct S {
	int x;
	float y;
};

int main() {
	auto [a, b] = S{2, 40.5f};
	return a + static_cast<int>(b);
}
