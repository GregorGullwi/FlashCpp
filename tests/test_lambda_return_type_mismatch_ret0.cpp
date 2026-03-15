// Test: lambda return type must not inherit the enclosing function's return type
// for sema annotation purposes.  The enclosing function returns long, but the
// lambda returns int.  If the sema pass leaks the enclosing return type into
// the lambda body, the lambda's "return x;" is annotated with int→long,
// producing a wrong result.
//
// The lambda must capture a variable from the enclosing scope (not use a lambda
// parameter) so that inferExpressionType can resolve its type via the scope
// stack — lambda parameters are not registered in the sema scope stack, so a
// lambda-parameter-only version silently avoids the bug.

long capture_and_return() {
	int x = 7;
	// Lambda captures x (int, visible in sema scope stack) and returns int.
	// Sema should NOT annotate this return with int→long, but the bug causes
	// it to use the enclosing function's return type (long) as the target.
	auto lam = [x]() -> int { return x; };
	int r = lam();          // r should be 7
	return (long)r;         // explicit conversion in the enclosing function
}

int main() {
	long v = capture_and_return();
	return (int)(v - 7L);   // expected: 0
}
