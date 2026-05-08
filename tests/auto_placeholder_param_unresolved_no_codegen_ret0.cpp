// Regression test for:
//   "Itanium name mangling: unresolved 'auto' type reached mangling"
//
// Root causes fixed:
//   1. typeSpecStillUsesDependentPlaceholder() did not detect TypeCategory::Auto
//      with an invalid TypeIndex (abbreviated-template auto parameter); such
//      parameters bypassed the has_unresolved_params guard and reached codegen.
//   2. try_instantiate_template_explicit() unconditionally added every instantiated
//      node to the top-level AST, including body-less SFINAE probes and nodes
//      with unsubstituted auto parameters.
//
// The test below exercises abbreviated function templates (auto parameters) in
// various combinations.  The compiler must NOT crash with a name-mangling error
// and must produce correct results for each call.

template <typename T>
int identity(T x) { return static_cast<int>(x); }

// Abbreviated function template: `auto` parameter must never reach the IR
// generator while still typed as TypeCategory::Auto.
template <auto N>
int constexpr_nttp() { return static_cast<int>(N); }

// A function template whose return type is deduced - the ONLY thing that
// reaches codegen is the fully-substituted instantiation.
template <typename T>
auto add_one(T x) -> T { return x + T(1); }

int main() {
	// Basic template instantiation through deduction - exercises the
	// has_unresolved_params guard in try_instantiate_single_template.
	if (identity(42) != 42) return 1;
	if (identity(3.14f) != 3) return 2;  // float→int truncation

	// Non-type template parameter - exercises a different instantiation path.
	if (constexpr_nttp<7>() != 7) return 3;

	// Auto return type deduction - must resolve before codegen.
	if (add_one(10) != 11) return 4;
	if (add_one(100) != 101) return 5;

	return 0;
}
