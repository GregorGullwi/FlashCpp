// Phase 6 regression: aggregate brace-init of a struct with a pack-expanded
// positional initializer.  Before the fix, `Triple t = {args...};` failed at
// parse time with "Failed to parse initializer expression" because the
// positional aggregate-init path in parse_brace_initializer did not recognise
// a trailing `...` on an expression.  After the fix, the element is wrapped in
// a PackExpansionExprNode and expanded into N initializers during template
// substitution, matching the 3 struct members.
struct Triple { int a; int b; int c; };

template<typename... Ts>
int sum_triple(Ts... args) {
	Triple t = {static_cast<int>(args)...};
	return t.a + t.b + t.c;
}

int main() {
	return sum_triple(10, 15, 17); // = 42
}
