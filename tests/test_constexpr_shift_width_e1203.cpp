// Reduced regression for DiagnosticId::ConstantExpressionShiftCountTooLarge (1203).
// C++20 [expr.shift]/1: shift behavior is undefined when the count is greater
// than or equal to the width of the promoted left operand (unsigned int here);
// such a constant expression must be rejected.
constexpr int value = 1u << 40;

int main() {
	return value;
}
