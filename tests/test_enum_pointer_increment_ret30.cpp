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
//    (operandHandledAsIdentifier == true), so this scenario stays as a guard
//    that the fallback path keeps working.
//
// 2. Non-identifier expression: ++(*pp) where pp is Color**
//    This is a regression guard for commit 8991bfd and its follow-up fixes:
//    *pp yields a TempVar-backed enum pointer result, so the symbol-table
//    fallback does NOT apply here (operandHandledAsIdentifier == false).
//    The ExprResult metadata path must preserve pointer_depth and pointee-size
//    information so generateBuiltinIncDec emits pointer-stride arithmetic.

enum Color { Red = 0,
			 Green = 10,
			 Blue = 20 };

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
 // The operand *pp is a TempVar-backed non-identifier, so this checks that
 // the fixed ExprResult metadata flow still drives pointer-stride arithmetic.
	++(*pp);
	int val2 = *p;  // Expected: 10 (Green) — p was modified via *pp

 // val1 + val2 + 10 = 10 + 10 + 10 = 30
 // If the regression returns, *pp will not advance by sizeof(Color),
 // *p will not read Green, and the return value won't be 30.
	return val1 + val2 + 10;
}
