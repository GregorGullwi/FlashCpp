// Regression test for SemanticAnalysis "determineConversionKind: unhandled type pair" crash.
//
// Bug: tryAnnotateConversion() only guarded against Struct/Enum/Invalid/Auto but NOT
// Type::UserDefined. When a lazily-instantiated function (e.g. a member of
// pointer_traits or __ptr_traits_ptr_to) had a return type that stayed as
// Type::UserDefined (template alias pattern like __ptr_traits_ptr_to$pattern___)
// and the return expression resolved to a different Type::UserDefined (raw template
// param _Tp), then:
//   can_convert_type(UserDefined, UserDefined) → ExactMatch (same enum value!)
//   determineConversionKind(UserDefined, UserDefined) → throw "unhandled type pair"
//
// Fix: replace the piecemeal negative guards with a positive numeric-only check so
// all non-numeric types (including UserDefined) are skipped cleanly.
//
// This test includes <bits/ptr_traits.h> which exercises the exact template pattern
// that creates two functions with different Type::UserDefined return types in ast_nodes_.

#include <bits/ptr_traits.h>

int main() {
	return 0;
}
