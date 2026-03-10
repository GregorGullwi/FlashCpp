// Regression test: passing a short to a function expecting int
// must sign-extend properly, not read garbage from adjacent stack bytes.

int addOne(int x) {
	return x + 1;
}

int main() {
	short s = 42;
	// 's' is 16-bit on the stack.  The call must promote it to 32-bit int.
	if (addOne(s) != 43) return 1;
	return 0;
}
