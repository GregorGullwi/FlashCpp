// Test: lambda return type must not inherit the enclosing function's return type
// for sema annotation purposes.  The enclosing function returns double, but the
// lambda returns int.  If the sema pass leaks the enclosing return type into
// the lambda body, the lambda's "return x;" is annotated with int→double,
// producing a wrong result.

double invoke_lambda(int val) {
	// Lambda explicitly returns int — no conversion should happen inside it.
	auto lam = [](int x) -> int { return x; };
	int result = lam(val);
	// The enclosing function returns double; only *this* return should convert.
	return result;
}

int main() {
	double d = invoke_lambda(42);
	// If the lambda incorrectly applied int→double inside its body,
	// the value would be mangled (e.g., truncated back from double bits).
	int check = (int)d;
	return check - 42;  // expected: 0
}
