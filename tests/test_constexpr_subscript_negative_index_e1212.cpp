// Reduced regression for DiagnosticId::ConstantExpressionArrayIndexOutOfBounds (1212).
// C++20 [expr.const]/4: a negative subscript cannot designate an element.
constexpr char letters[4] = {'a', 'b', 'c', 'd'};
constexpr char bad = letters[-1];

int main() {
	return 0;
}
