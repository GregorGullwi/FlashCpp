// Test immediate constexpr lambda invocation

constexpr int testImmediateLambda() {
	auto f = []() { return 42; };
	return f();
}
static_assert(testImmediateLambda() == 42,
			  "immediately-invoked constexpr lambda should work");

constexpr int testImmediateLambdaCapture() {
	constexpr int base = 40;
	auto f = [base]() {
		int y = 2;
		return base + y;
	};
	return f();
}
static_assert(testImmediateLambdaCapture() == 42,
			  "immediately-invoked constexpr lambda should support captures and local variables");

int main() {
	if (testImmediateLambda() != 42) return 1;
	if (testImmediateLambdaCapture() != 42) return 1;
	return 0;
}