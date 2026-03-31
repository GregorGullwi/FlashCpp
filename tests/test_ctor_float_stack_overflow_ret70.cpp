// Test: constructor call with float argument overflowing to the stack,
// followed by another stack argument.
//
// This test exercises the stack_arg_count increment for float args in
// handleConstructorCall's first pass.  Without the fix, the float branch
// does not increment stack_arg_count, so the subsequent int argument on
// the stack overwrites the float at the same RSP offset.
//
// On SysV AMD64: 'this' takes RDI (int reg 0), leaving 5 int regs
// (RSI, RDX, RCX, R8, R9) and 8 XMM regs (XMM0-XMM7) for explicit
// parameters.  We use 5 ints (fill all remaining int regs), 9 doubles
// (8 in XMMs, 1 on stack), and 1 final int (on stack).
//
// Correct layout:
//   stack slot 0: f9 (9th double, overflows XMM regs)
//   stack slot 1: last (6th int, overflows int regs)
//
// Bug layout (without fix):
//   stack slot 0: f9 written, then last overwrites it at same offset

struct ManyParams {
	int sum;
	ManyParams(int a, int b, int c, int d, int e,
			   double f1, double f2, double f3, double f4,
			   double f5, double f6, double f7, double f8,
			   double f9, int last) {
	// a-e in int regs (RSI-R9, since RDI='this')
	// f1-f8 in XMM0-XMM7
	// f9 on stack (stack slot 0)
	// last on stack (stack slot 1)
		sum = a + b + c + d + e + last;
	// Add truncated doubles
		sum = sum + (int)f1 + (int)f2 + (int)f3 + (int)f4;
		sum = sum + (int)f5 + (int)f6 + (int)f7 + (int)f8;
		sum = sum + (int)f9;
	}
};

int main() {
 // a=1, b=2, c=3, d=4, e=5 -> sum of ints = 1+2+3+4+5 = 15
 // f1..f8 = 1.0..8.0 -> sum = 1+2+3+4+5+6+7+8 = 36
 // f9 = 9.0, last = 10 -> +9 +10 = 19
 // Total: 15 + 36 + 19 = 70
	ManyParams m(1, 2, 3, 4, 5,
				 1.0, 2.0, 3.0, 4.0,
				 5.0, 6.0, 7.0, 8.0,
				 9.0, 10);
	return m.sum; // Expected: 70
}
