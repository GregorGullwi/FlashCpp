// Test: first-pass / second-pass param_shift desync in handleFunctionCall.
//
// This exercises a pre-existing bug where the first pass (stack overflow
// detection) starts temp_int_idx at 0, but the second pass (register loading)
// starts int_reg_index at param_shift (1 when the function returns a large
// struct by value via hidden return parameter).
//
// On SysV AMD64:
//   - Hidden return param takes RDI (int reg 0) → param_shift = 1
//   - 6 integer parameter registers total: RDI, RSI, RDX, RCX, R8, R9
//   - First pass thinks 6 explicit args fit (temp_int_idx: 0→1→2→3→4→5→6, all < 6+1=7... wait, it checks > max_int_regs=6)
//     Actually: temp_int_idx starts at 0. For each of 6 args, it checks temp_int_idx(0..5) + 1 > 6? No. So none go on stack.
//   - Second pass starts int_reg_index = 1. For each of 6 args, it checks int_reg_index + 1 <= 6.
//     Args 1-5 fit (indices 1..5). Arg 6: int_reg_index=6, 6+1>6 → !use_register → skipped.
//   - Result: arg 6 is never placed on the stack (first pass) AND never loaded into a register (second pass).
//     The callee reads garbage for that parameter.
//
// The function make_big returns a >16 byte struct (triggers hidden return param)
// and takes 6 int args. The 6th arg (f=7) should contribute to the sum but
// won't if the bug is present.
//
// If the bug is present:
//   The 6th argument (7) is never passed. The callee reads garbage (likely 0),
//   so the result will be 1+2+3+4+5+0 = 15, not 42 → return value ≠ 42.
//
// If the bug is fixed:
//   All 6 arguments are correctly passed. 1+2+3+4+5+7 = 22.
//   make_big returns BigStruct{22, 20, 0}.
//   main() returns result.x + result.y = 22 + 20 = 42.

struct BigStruct {
	long long x;
	long long y;
	long long z;
};

BigStruct make_big(int a, int b, int c, int d, int e, int f) {
	BigStruct s;
	s.x = a + b + c + d + e + f;
	s.y = 20;
	s.z = 0;
	return s;
}

int main() {
	BigStruct result = make_big(1, 2, 3, 4, 5, 7);
	return (int)(result.x + result.y); // Expected: 22 + 20 = 42
}
