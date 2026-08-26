// Reduced regression for DiagnosticId::ConstantExpressionArrayIndexOutOfBounds (1212).
// C++20 [expr.const]/4: the first subscript of a local multi-dimensional
// array must still be bounds-checked.
constexpr int readGrid() {
	int grid[2][2] = {{1, 2}, {3, 4}};
	return grid[2][0];
}

static_assert(readGrid() == 0);

int main() {
	return 0;
}
