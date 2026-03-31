// Test: Enum IR lowering migration coverage.
// Verifies that operator overload applicability, static cast identity,
// and conversion operator fallback suppression all work correctly after
// migrating Type::Enum/UserDefined checks to carriesSemanticTypeIndex()
// and isIrStructType(toIrType()) helpers.

enum Color { Red = 0,
			 Green = 1,
			 Blue = 2 };
enum class Shape : int { Circle = 10,
						 Square = 20,
						 Triangle = 30 };

// User-defined operator overloads for enum types
// Tests: typeSpecRequiresUserDefinedOperator / hasUserDefinedIdentityFromIr
// now using carriesSemanticTypeIndex() instead of Type::Struct || Type::Enum
Color operator+(Color a, Color b) {
	return static_cast<Color>(static_cast<int>(a) + static_cast<int>(b));
}

bool operator<(Color a, Color b) {
	return static_cast<int>(a) < static_cast<int>(b);
}

// Static cast tests (exercises source_has_semantic_identity lambda
// now using carriesSemanticTypeIndex())
int test_static_cast_enum_to_int() {
	Color c = Blue;
	int val = static_cast<int>(c);
	return val == 2 ? 0 : 1;
}

int test_static_cast_int_to_enum() {
	int val = 1;
	Color c = static_cast<Color>(val);
	return c == Green ? 0 : 2;
}

int test_static_cast_scoped_enum() {
	Shape s = Shape::Square;
	int val = static_cast<int>(s);
	Shape s2 = static_cast<Shape>(30);
	return (val == 20 && s2 == Shape::Triangle) ? 0 : 3;
}

// Enum operator overload test
int test_enum_user_defined_operator() {
	Color result = Red + Green;	// Should call operator+(Color, Color)
	return result == Green ? 0 : 4;
}

int test_enum_user_defined_comparison() {
	bool result = Red < Blue;  // Should call operator<(Color, Color)
	return result ? 0 : 5;
}

int main() {
	int r = 0;
	r = test_static_cast_enum_to_int();
	if (r != 0)
		return r;
	r = test_static_cast_int_to_enum();
	if (r != 0)
		return r;
	r = test_static_cast_scoped_enum();
	if (r != 0)
		return r;
	r = test_enum_user_defined_operator();
	if (r != 0)
		return r;
	r = test_enum_user_defined_comparison();
	if (r != 0)
		return r;
	return 0;
}
