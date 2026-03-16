// Regression test: function-try-block in a full template specialization.
// The guard at Parser_Templates_Class.cpp should accept 'try' in addition to '{'.
// Without the fix, this fails at parse time with:
//   "Template specializations must have a definition (body), found 'try'"

// Primary template: normal body
template<typename T>
T safe_divide(T a, T b) {
	return a / b;
}

// Full specialization with function-try-block
template<>
int safe_divide<int>(int a, int b)
try {
	if (b == 0) throw 42;
	return a / b;
} catch (int e) {
	return -e;
}

int main() {
	// Normal path
	if (safe_divide<int>(10, 2) != 5) return 1;

	// Exception path
	if (safe_divide<int>(10, 0) != -42) return 2;

	return 0;
}
