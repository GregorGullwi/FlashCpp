// Regression test: return from inside a nested catch handler.
// On Windows FH3, the inner catch must resume at an outer-catch-local
// continuation point that can still complete the parent-function return.

int nested_catch_return(int x) {
	try {
		throw 1;
	} catch (int outer_val) {
		try {
			throw 2;
		} catch (int inner_val) {
			return x + outer_val + inner_val;
		}
	}
	return -1;
}

int main() {
	int result = nested_catch_return(100);
	if (result != 103) return 1;
	return 0;
}