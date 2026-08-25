// Reduced regression for DiagnosticId::ConstantExpressionShiftCountTooLarge (1203).
// (1u + 1u) has type unsigned int; shift count 40 >= 32 must be rejected even
// when the left operand is produced by arithmetic rather than a literal.
static_assert((1u + 1u) << 40, "shift count too large");

int main() {
	return 0;
}
