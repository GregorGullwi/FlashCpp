// Test: constexpr callable object initialized with explicit constructor
struct Mul {
	int factor;
	constexpr Mul(int f) : factor(f) {}
	constexpr int operator()(int a, int b) const { return (a + b) * factor; }
};

constexpr Mul mul = Mul(1);
constinit int x = mul(20, 22);  // (20+22)*1 = 42

int main() {
	return x;
}
