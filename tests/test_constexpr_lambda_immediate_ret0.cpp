// Test immediate constexpr lambda invocation

static_assert([]() { return 42; }() == 42,
			  "immediately-invoked constexpr lambda should work");

constexpr int base = 40;
static_assert([base]() {
	int y = 2;
	return base + y;
}() == 42,
			  "immediately-invoked constexpr lambda should support captures and local variables");

int main() {
	return 0;
}