// Test that calling a consteval function with a non-constant argument is ill-formed.
// C++20 [dcl.consteval]: an immediate invocation shall be a constant expression;
// passing a non-const variable makes the call non-constant — compile error.

int runtime_val = 5;

consteval int double_it(int x) { return x * 2; }

int main() {
	return double_it(runtime_val); // ERROR: not a constant expression
}
