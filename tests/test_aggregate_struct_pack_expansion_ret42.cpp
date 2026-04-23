// Phase 6 regression: aggregate brace-init of a struct with a pack-expanded
// positional initializer.  Before the fix, `Triple t = {args...};` failed at
// parse time with "Failed to parse initializer expression" because the
// positional aggregate-init path in parse_brace_initializer did not recognise
// a trailing `...` on an expression.  After the fix, the element is wrapped in
// a PackExpansionExprNode and expanded into N initializers during template
// substitution, matching the 3 struct members.
//
// Also exercises the follow-on Gemini review fix: elements after the pack
// expansion (e.g. `{args..., extra}`) are now parsed and preserved correctly
// instead of being rejected with "Too many initializers".
struct Triple { int a; int b; int c; };
struct Quad   { int a; int b; int c; int d; };

template<typename... Ts>
int sum_triple(Ts... args) {
	Triple t = {static_cast<int>(args)...};
	return t.a + t.b + t.c;
}

// pack expansion followed by a trailing non-pack element
template<typename... Ts>
int sum_quad_with_extra(Ts... args) {
	Quad q = {static_cast<int>(args)..., 100};
	return q.a + q.b + q.c + q.d;
}

int main() {
	int r1 = sum_triple(10, 15, 17);                 // = 42
	int r2 = sum_quad_with_extra(10, 15, 17);        // = 142
	return (r1 == 42 && r2 == 142) ? 42 : -1;
}
