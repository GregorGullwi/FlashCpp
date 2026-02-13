// Test: function-like macro name on one line, arguments on the next
// Per C standard §6.10.3, if the identifier is a function-like macro name
// and the next preprocessing token is '(' (even on the next line),
// it forms a macro invocation.
#define ADD(a, b) ((a) + (b))
#define MUL(a, b) ((a) * (b))

// Macro name on one line, '(' on next line
int x = ADD
  (3, 4);

// Same but with more whitespace
int y = MUL
    (5, 2);

int main() {
	if (x != 7) return 1;
	if (y != 10) return 2;
	return 0;
}
