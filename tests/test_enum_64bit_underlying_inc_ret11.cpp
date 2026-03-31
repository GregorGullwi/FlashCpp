// Regression guard for the toExprResult slot-4 heuristic with 64-bit enums.
//
// The heuristic at IROperandHelpers.h uses size_in_bits==64 && metadata>0
// && Type::Enum to decide that slot-4 carries pointer_depth (i.e., the
// operand is an enum pointer). For a non-pointer enum with a 64-bit
// underlying type (e.g., enum : long long), the encoder lowers the type
// to the underlying integer type (Type::LongLong), so the heuristic's
// Type::Enum check should not fire for simple identifiers.
//
// However, if any code path preserves Type::Enum for a 64-bit non-pointer
// enum value with nonzero type_index, the heuristic would misinterpret
// type_index as pointer_depth, causing generateBuiltinIncDec to emit
// pointer-stride arithmetic instead of integer increment.
//
// This test exercises:
// 1. Basic arithmetic on enum : long long values
// 2. Comparison of enum : long long values
// 3. Casting between enum : long long and its underlying type

enum BigColor : long long { Red = 0,
							Green = 10,
							Blue = 20 };

int main() {
 // --- Scenario 1: basic enum value usage ---
	BigColor c = Red;
	int val1 = static_cast<int>(c);	// Expected: 0

 // --- Scenario 2: enum arithmetic via cast ---
	BigColor c2 = Green;
	long long raw = static_cast<long long>(c2);
	raw = raw + 1;  // 10 + 1 = 11
	int val2 = static_cast<int>(raw);  // Expected: 11

 // --- Scenario 3: enum comparison ---
	BigColor c3 = Blue;
	if (c3 != Blue) {
		return 99;  // Should not reach here
	}

 // val1 + val2 = 0 + 11 = 11
	return val1 + val2;
}
