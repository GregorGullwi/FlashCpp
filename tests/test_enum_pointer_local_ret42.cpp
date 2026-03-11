// Regression test for ExprResult migration (Phase 2):
// Local enum-pointer variables (VariableDeclarationNode path) must preserve
// pointer_depth in the 4th operand slot, not type_index.  The ExprResult
// conversion operator sees Type::Enum and defaults to type_index; without
// preserveLegacyEnumPointerDepthEncoding the pointer_depth is dropped to 0,
// breaking dereference and pointer-arithmetic codegen.
//
// This test forces the enum pointer through toTypedValue by:
// 1. Passing it to a function (function arg path)
// 2. Using pointer arithmetic (binary op path)

enum Color { Red = 10, Green = 20, Blue = 42 };

int read_color(Color* p) {
    return *p;
}

int main() {
    Color colors[3] = {Red, Green, Blue};
    Color* p = &colors[0];

    // Force enum pointer through toTypedValue via function call
    int a = read_color(p);         // 10

    // Force enum pointer through toTypedValue via pointer arithmetic
    Color* p2 = p + 2;
    int b = *p2;                   // 42

    // Combine: 10 + (42 - 10) = 42
    return a + (b - a);
}
