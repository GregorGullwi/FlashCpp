// Test: lambda return type must not inherit the enclosing function's return type
// for sema annotation purposes.  The enclosing function returns double, but the
// lambda returns int.  If the sema pass leaks the enclosing return type into
// the lambda body, the lambda's "return x;" is annotated with int→double,
// which would move the integer value into an XMM register.  When the lambda's
// ReturnOp is typed as int (32-bit GPR), the backend would read garbage from
// the wrong register class.
//
// The lambda must capture a variable from the enclosing scope (not use a lambda
// parameter) so that inferExpressionType can resolve its type via the scope
// stack — lambda parameters are not registered in the sema scope stack, so a
// lambda-parameter-only version silently avoids the bug.

double capture_and_return() {
	int x = 7;
	// Lambda captures x (int, visible in sema scope stack) and returns int.
	// Sema should NOT annotate this return with int→double, but the bug causes
	// it to use the enclosing function's return type (double) as the target.
	auto lam = [x]() -> int { return x; };
	int r = lam();          // r should be 7
	return (double)r;       // explicit conversion in the enclosing function
}

int main() {
	double v = capture_and_return();
	int check = (int)v;
	return check - 7;       // expected: 0
}
