// Test C++20 [expr.unary.op] integral promotions for unary +, -, ~
// The operand of unary +, -, ~ undergoes integral promotion before the operation.
// This means types smaller than int (char, short, bool, unsigned char, etc.)
// are promoted to int BEFORE the operator is applied.

int main() {
	// Test 1: Bitwise NOT on unsigned char
	// Per C++20: uc promotes to int (255), then ~255 = -256
	// Bug case: if NOT is done in 8 bits, ~0xFF = 0x00, then zero-extended to int = 0
	unsigned char uc = 0xFF;
	int complement = ~uc;
	if (complement != -256) return 1;

	// Test 2: Unary plus on char (simple promotion check)
	char c = 5;
	int promoted = +c;
	if (promoted != 5) return 2;

	// Test 3: Unary minus on short
	short s = -10;
	int negated = -s;
	if (negated != 10) return 3;

	// Test 4: Bitwise NOT on short
	// short(0) promoted to int(0), then ~0 = -1
	short zero = 0;
	int not_zero = ~zero;
	if (not_zero != -1) return 4;

	// Test 5: Bitwise NOT on bool
	// bool(true) promoted to int(1), then ~1 = -2
	bool b = true;
	int not_b = ~b;
	if (not_b != -2) return 5;

	// Test 6: Unary minus on unsigned char
	// uc promoted to int(200), then negated = -200
	unsigned char uc2 = 200;
	int neg_uc = -uc2;
	if (neg_uc != -200) return 6;

	return 0;
}
