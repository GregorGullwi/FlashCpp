// Test: constexpr callable object initialized with ConstructorCallNode can be used as constinit initializer
struct Add {
	constexpr Add() {}
	constexpr int operator()(int a, int b) const { return a + b; }
};

constexpr Add add{};
constinit int x = add(40, 2);

int main() {
	return x;  // should be 42
}
