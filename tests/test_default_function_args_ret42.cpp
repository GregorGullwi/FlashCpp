// Test: default function arguments
// Verifies that functions with default parameter values work correctly
// when called with fewer arguments than declared parameters.

int add(int a, int b = 32) {
	return a + b;
}

int add3(int a, int b = 10, int c = 22) {
	return a + b + c;
}

// All defaults
int getVal(int x = 42) {
	return x;
}

int main() {
	int r1 = add(10);			  // 10 + 32 = 42
	int r2 = add(20, 22);		  // 20 + 22 = 42
	int r3 = add3(10, 10);		   // 10 + 10 + 22 = 42
	int r4 = add3(10, 10, 22);	   // 10 + 10 + 22 = 42
	int r5 = getVal();			   // 42
	int r6 = getVal(42);			 // 42

	if (r1 != 42)
		return 1;
	if (r2 != 42)
		return 2;
	if (r3 != 42)
		return 3;
	if (r4 != 42)
		return 4;
	if (r5 != 42)
		return 5;
	if (r6 != 42)
		return 6;
	return 42;
}
