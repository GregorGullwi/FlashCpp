// Test that an explicit-template-argument consteval function call succeeds when
// the argument is a constant expression.

template<typename T>
consteval int double_it(T x) { return static_cast<int>(x) * 2; }

constexpr int folded = double_it<int>(21);

int main() {
	return folded == 42 ? 0 : 1;
}
