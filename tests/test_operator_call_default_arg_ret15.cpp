// Test operator() with default argument
// operator()(int x, int y = 10) called as c(5) should use y=10 and return 15.
// This verifies that default arguments are correctly appended when
// resolve_overload selects an operator() overload via countMinRequiredArgs.

struct Caller {
	int operator()(int x, int y = 10) {
		return x + y;
	}
};

int main() {
	Caller c;
	return c(5);  // Expected: 5 + 10 = 15
}
