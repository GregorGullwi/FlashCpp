// Floating-point operands are rejected for '~' during semantic promotion and
// must report BitwiseNotRequiresIntegral.

int main() {
	double value = 1.5;
	return ~value;
}
