// Regression for DiagnosticId::ConstantExpressionSignedIntegerOverflow (1205).
// INT_MIN / -1 overflows for 32-bit signed int in constant expressions.
constexpr int value = (-2147483647 - 1) / -1;

int main() {
	return value;
}
