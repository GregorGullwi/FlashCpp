// Reduced regression for DiagnosticId::ConstantExpressionArrayIndexOutOfBounds (1212).
// C++20 [expr.const]/4: storing through an out-of-bounds subscript inside a
// constexpr function is rejected during evaluation of the assertion.
constexpr int writeLocal() {
	unsigned values[3] = {1, 2, 3};
	values[5] = 9;
	return static_cast<int>(values[0]);
}

static_assert(writeLocal() == 1);

int main() {
	return 0;
}
