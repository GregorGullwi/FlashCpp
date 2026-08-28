// Test that calling a consteval function template with explicit template arguments
// and a non-constant runtime argument is ill-formed. C++20 [dcl.consteval]:
// every immediate invocation must be a constant expression.

int runtime_val = 5;

template<typename T>
consteval int double_it(T x) { return static_cast<int>(x) * 2; }

int main() {
	return double_it<int>(runtime_val); // ERROR: not a constant expression
}
