// Reduced regression for DiagnosticId::ConstantExpressionDivisionByZero (1201).
// C++20 [expr.const]/4: division by zero is not a constant expression.
constexpr int value = 1 / 0;

int main() {
	return value;
}
