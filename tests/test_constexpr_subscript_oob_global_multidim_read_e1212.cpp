// Reduced regression for DiagnosticId::ConstantExpressionArrayIndexOutOfBounds (1212).
// C++20 [expr.const]/4: a subscript on a global multidimensional constexpr array
// must be bounds-checked even when it is used in a constexpr variable initializer.
constexpr int grid[2][2] = {{1, 2}, {3, 4}};
static_assert(grid[2][0] == 0);

int main() {
	return 0;
}
