// Reduced regression for DiagnosticId::ConstantExpressionModuloByZero (1202).
// C++20 [expr.const]/4: the second operand of % must be non-zero.
constexpr int value = 1 % 0;

int main() {
	return value;
}
