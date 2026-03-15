// Regression test: tryAnnotateConversion() must not crash when the expression
// type or the target type is Type::UserDefined (typedef / type-alias).
// Before the fix, can_convert_type(UserDefined, UserDefined) returned ExactMatch
// for any two UserDefined types regardless of their type_index, so the
// expr_type_id == target_type_id early-exit did not fire, and execution reached
// determineConversionKind(UserDefined, UserDefined) which threw InternalError.
//
// The test passes the annotated code path: a function whose parameter type is a
// typedef-aliased integer (stored as Type::UserDefined in the symbol table) is
// called with an expression of a different UserDefined typedef, triggering the
// tryAnnotateConversion guard that now returns false before reaching
// determineConversionKind.

typedef int my_int;
typedef long my_long;

my_long identity_long(my_long x) { return x; }

int main() {
	my_int  a = 3;
	my_long b = identity_long(a);  // my_int → my_long: UserDefined guard fires, no crash
	return (int)(b - 3);
}
