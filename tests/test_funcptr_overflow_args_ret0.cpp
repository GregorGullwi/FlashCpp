// Test: indirect call through function pointer with more arguments than
// fit in registers.  On Linux SysV the first 6 integer args go in
// registers; the 7th must be spilled to the stack.  On Windows the
// first 4 go in registers; the 5th overflows.
//
// This exercises the overflow-argument spill path in handleIndirectCall
// which, at the time of writing, silently drops args that exceed
// register capacity.

int add_seven(int a, int b, int c, int d, int e, int f, int g) {
	return a + b + c + d + e + f + g;
}

int main() {
	int (*fp)(int, int, int, int, int, int, int) = add_seven;
	int result = fp(1, 2, 3, 4, 5, 6, 7);
	// 1+2+3+4+5+6+7 = 28
	return (result == 28) ? 0 : 1;
}
