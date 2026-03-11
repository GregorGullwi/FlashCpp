// Regression test for ExprResult migration (Phase 2):
// Local enum-pointer variables (VariableDeclarationNode path) must preserve
// pointer_depth in the 4th operand slot, not type_index.  The ExprResult
// conversion operator sees Type::Enum and defaults to type_index; without
// preserveLegacyEnumPointerDepthEncoding the pointer_depth is dropped to 0,
// breaking dereference and pointer-arithmetic codegen.

enum Color { Red = 10, Green = 20, Blue = 42 };

int main() {
    Color c = Blue;
    Color* p = &c;
    return *p;
}
