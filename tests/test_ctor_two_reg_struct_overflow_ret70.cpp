// Test: constructor call where a two-register struct partially overflows
// the register file, followed by another integer argument.
//
// This exercises a bug in handleConstructorCall's second pass where
// int_reg_index is not advanced when a two-register struct doesn't fit
// in registers.  The first pass correctly advances temp_int_idx by 2
// (line 997), but the second pass simply skips the if-block without
// incrementing int_reg_index when int_reg_index + regs_needed > max_int_regs.
//
// On SysV AMD64: 'this' takes RDI (int reg 0), leaving 5 int regs
// (RSI, RDX, RCX, R8, R9) for explicit parameters.
//
// Constructor signature: Receiver(int a, int b, int c, int d, Big3 s, int last)
//   - a → RSI  (int_reg 1)
//   - b → RDX  (int_reg 2)
//   - c → RCX  (int_reg 3)
//   - d → R8   (int_reg 4)
//   - Big3 s needs 2 regs, but only R9 (int_reg 5) is left → overflow to stack
//   - last also overflows to stack (int_reg 7 > 6)
//
// First pass (stack placement):
//   temp_int_idx: 1→2→3→4→5→7(+2 for Big3)→8(+1 for last)
//   Big3 goes to stack slot 0+1, last goes to stack slot 2
//
// Second pass (register loading) — THE BUG:
//   int_reg_index starts at 1, advances to 5 after a,b,c,d
//   Big3: 5+2>6 → skip. But int_reg_index stays at 5!
//   last: 5+1<=6 → true! → incorrectly loaded into R9
//   But first pass already put 'last' on the stack → callee reads stack → wrong value
//
// If the bug is present:
//   'last' (value 50) is loaded into R9 instead of being on the stack.
//   The callee reads 'last' from the stack where it was placed by pass 1,
//   but pass 2 also clobbered R9 with 'last' instead of leaving it alone.
//   Depending on what garbage is on the stack at that slot, the result will
//   differ from 70.
//
// If the bug is fixed:
//   int_reg_index advances by 2 for the skipped Big3, reaching 7.
//   'last' then also correctly identifies as a stack arg (7+1>6), and
//   no register is incorrectly loaded.  Result = 1+2+3+4+3+0+7+50 = 70.

struct Big3 {
	int a;
	int b;
	int c;
};

struct Receiver {
	int result;
	Receiver(int a, int b, int c, int d, Big3 s, int last) {
		result = a + b + c + d + s.a + s.b + s.c + last;
	}
};

int main() {
	Big3 s = {3, 0, 7};
	Receiver r(1, 2, 3, 4, s, 50);
	return r.result; // Expected: 1+2+3+4+3+0+7+50 = 70
}
