// Reduced regression for DiagnosticId::ConstantExpressionArrayIndexOutOfBounds (1212).
// C++20 [expr.const]/4, [expr.add]/4: reading past the end of a constexpr
// array is not a constant expression and must be rejected eagerly, mixing a
// wider element type than int.
constexpr long samples[3] = {10, 20, 30};
constexpr long bad = samples[5];

int main() {
	return 0;
}
