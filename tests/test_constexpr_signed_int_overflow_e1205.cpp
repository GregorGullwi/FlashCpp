// Regression for DiagnosticId::ConstantExpressionSignedIntegerOverflow (1205).
// C++20 [expr.const]/4: signed integer overflow is not a constant expression.
constexpr int value = 2000000000 + 2000000000;

int main() {
	return value;
}
