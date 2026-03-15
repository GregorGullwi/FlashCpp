// Regression test: _Tp(x)[N] at statement level is a DECLARATION per C++20
// [dcl.ambig.res] and must NOT be routed to parse_expression_statement.
//
// Previously, '[' was in the expression-continuation list for the template type
// parameter disambiguation code in parse_statement_or_declaration(). This caused
// _Tp(x)[3] to be misrouted to parse_expression_statement, where it would be
// interpreted as: cast x to type _Tp, then subscript the result with 3. That
// routing produced a bad object (codegen error: "Symbol 'x' not found") which
// caused a runtime segfault.
//
// Per C++20 [dcl.ambig.res]: _Tp(x)[3] where _Tp is a template type param and
// x is an identifier declares x as _Tp[3] (array of 3 _Tp). The standard
// mandates treating the ambiguous statement as a declaration. '[' is NOT an
// expression-only token and must not trigger expression-statement routing.
//
// After the fix, the statement is correctly routed to parse_variable_declaration.
// FlashCpp does not yet support the parenthesized declarator form _Tp(x)[3]
// (it expects just _Tp x[3]), so the compile fails with a parse error rather
// than a codegen crash. This _fail.cpp records the correct failure mode.

template<typename T>
struct Test {
	static int run() {
		T(x)[3];   // C++20: declaration, NOT an expression
		return 0;
	}
};

int main() {
	return Test<int>::run();
}
