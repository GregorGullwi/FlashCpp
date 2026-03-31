// Regression test: when the divisor expression evaluates into RAX
// (e.g. a function return value), setupAndLoadArithmeticOperation
// moves the dividend into RAX before IDIV — silently overwriting the
// divisor.  The result should be 25/5=5; without the fix it is 1
// (RAX/RAX) or crashes.
int get_divisor() { return 5; }

int main() {
	int result = 25 / get_divisor();
	return result;  // expected: 5
}
