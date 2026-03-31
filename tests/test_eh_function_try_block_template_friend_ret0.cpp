// Regression test: function-try-block in a template friend function definition.
// The skip loop in parse_template_friend_declaration must handle function-try-blocks
// (i.e. stop at 'try' in addition to '{', and skip catch clauses after the body).
// Without the fix, the parser sees 'catch' as a struct member and fails to parse
// the struct, so even a trivial main() that constructs a Widget would not compile.

struct Widget {
	int value;
	Widget(int v) : value(v) {}

 // Template friend function with function-try-block body.
 // The parser skips this body during the first pass; it only needs to
 // correctly skip past 'try { ... } catch(...) { ... }' without
 // mistaking 'catch' for a struct member.
	template <typename T>
	friend T safe_add(Widget& w, T b) try {
		if (b < (T)0)
			throw -1;
		return w.value + b;
	} catch (int e) {
		return (T)e;
	}
};

int main() {
	Widget w(10);
 // Call via ADL (Widget& argument makes the hidden friend visible)
	if (safe_add(w, 5) != 15)
		return 1;
	if (safe_add(w, -1) != -1)
		return 2;
	return 0;
}
