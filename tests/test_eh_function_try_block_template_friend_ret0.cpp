// Regression test: function-try-block in a template friend function.
// The skip loop in parse_template_friend_declaration must handle function-try-blocks
// (i.e. stop at 'try' in addition to '{', and skip catch clauses after the body).
// Without the fix, the parser sees 'catch' as a struct member and fails.

int g_friend_result = 0;

struct Widget {
	int value;
	Widget(int v) : value(v) {}

	// Template friend function with function-try-block body
	template<typename T>
	friend T safe_add(T a, T b)
	try {
		if (b < (T)0) throw -1;
		return a + b;
	} catch (int e) {
		g_friend_result = e;
		return (T)-1;
	}
};

int main() {
	// Normal path
	if (safe_add(3, 4) != 7) return 1;

	// Exception path
	if (safe_add(3, -1) != -1) return 2;
	if (g_friend_result != -1) return 3;

	return 0;
}
