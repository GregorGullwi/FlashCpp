// Reduced regression for DiagnosticId::ConstantExpressionShiftOperationInvalid (1204).
// C++20 [expr.const]/4: an overflowed constant-integral operation is not a
// constant expression; 1 << 32 overflows the promoted int width.
constexpr int value = 1 << 32;

int main() {
	return value;
}
