// Test: constructor with double& parameter.
//
// This exercises the bug where handleConstructorCall's second pass
// defines is_float_arg without checking !arg.is_reference().
// The first pass correctly treats double& as an integer argument
// (references are pointers passed in GP registers), but the second
// pass treats it as a float and routes it to an XMM register.
// The callee expects the address in an integer register (RSI on
// SysV AMD64), so the write goes nowhere useful.
//
// If the bug is present:
//   - The double& parameter is loaded into XMM0 instead of RSI
//   - The constructor receives garbage as the pointer and either
//     crashes or fails to modify 'd'
//   - main() returns something other than 42
//
// If the bug is fixed:
//   - double& is treated as an integer (pointer) argument
//   - The constructor correctly receives the address and writes 42.0
//   - main() returns 42

struct Writer {
	int dummy;
	Writer(double& out) {
		out = 42.0;
	}
};

int main() {
	double d = 0.0;
	Writer w(d);
	return (int)d; // Expected: 42
}
