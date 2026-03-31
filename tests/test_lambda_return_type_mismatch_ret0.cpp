// Test: lambda return type must not inherit the enclosing function's return type
// for sema annotation purposes.  The enclosing function returns double, but the
// lambda's return type is deduced as int (auto).
//
// With the bug: sema annotates `return x` inside the lambda with int→double
// (the enclosing function's return type).  Codegen applies the conversion
// *before* auto-deduction runs, so auto-deduction sees a double operand and
// deduces the lambda's return type as double.  The lambda now returns a double
// in an XMM register when callers expect an int in a GPR → garbage.
//
// The lambda must capture a variable from the enclosing scope (not use a lambda
// parameter) so that inferExpressionType can resolve its type via the scope
// stack — lambda parameters are not registered in the sema scope stack, so a
// lambda-parameter-only version silently avoids the bug.

double capture_and_return() {
	int x = 7;
 // Lambda captures x (int, visible in sema scope stack).
 // Return type is auto-deduced as int — no conversion should happen.
 // With the bug, sema annotates this return with int→double.
	auto lam = [x]() { return x; };
	int r = lam();		   // r should be 7
	return (double)r;		  // explicit conversion in the enclosing function
}

int main() {
	double v = capture_and_return();
	int check = (int)v;
	return check - 7;		  // expected: 0
}
