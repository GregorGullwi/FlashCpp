// Test that ++/-- on enum pointers uses pointer-stride arithmetic
// (advances by sizeof(enum), not by 1 byte).
//
// This exercises the code path in generateBuiltinIncDec where
// getOperandPointerDepth must correctly report pointer_depth > 0
// for enum pointer types.
//
// Two scenarios are tested:
//
// 1. Simple identifier: ++p where p is Color*
//    This is covered by the symbol-table fallback in generateBuiltinIncDec
//    (operandHandledAsIdentifier == true), so it works even if toExprResult
//    misdecodes slot-4.
//
// 2. Non-identifier expression: ++(*pp) where pp is Color**
//    *pp yields a TempVar with Type::Enum and pointer_depth=1 in slot 4.
//    toExprResult misinterprets this as type_index=1, pointer_depth=0
//    for Enum types. The symbol-table fallback does NOT apply here
//    (operandHandledAsIdentifier == false), so the bug is exposed:
//    generateBuiltinIncDec sees pointer_depth=0 and emits integer
//    increment instead of pointer-stride arithmetic.

enum Color { Red = 0, Green = 10, Blue = 20 };

int main() {
    Color arr[3];
    arr[0] = Red;
    arr[1] = Green;
    arr[2] = Blue;

    // --- Scenario 1: simple identifier (masked by symbol-table fallback) ---
    Color* p = &arr[0];
    ++p;
    int val1 = *p;  // Expected: 10 (Green)

    // --- Scenario 2: non-identifier via Color** dereference ---
    // Reset p to start of array
    p = &arr[0];
    Color** pp = &p;

    // ++(*pp) should increment the Color* that pp points to,
    // advancing it by sizeof(Color) = 4 bytes to point to arr[1].
    // The operand *pp is a TempVar (not an identifier), so
    // operandHandledAsIdentifier is false and the symbol-table
    // fallback does not apply. This exposes the toExprResult bug.
    ++(*pp);
    int val2 = *p;  // Expected: 10 (Green) — p was modified via *pp

    // val1 + val2 + 10 = 10 + 10 + 10 = 30
    // If the bug is present, *pp gets integer-incremented by 1 byte
    // instead of sizeof(Color)=4 bytes, and *p reads garbage != 10,
    // so the return value won't be 30.
    return val1 + val2 + 10;
}
