// The operand of '~' must have integral or unscoped enumeration type. A class
// operand is rejected in codegen, which must report BitwiseNotRequiresIntegral
// instead of an unlocated string.

struct NotBitwise {
	int value;
};

int main() {
	NotBitwise operand{1};
	return ~operand;
}
