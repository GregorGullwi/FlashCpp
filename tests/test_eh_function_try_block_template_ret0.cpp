// Regression test: function-level try blocks in template functions.
// Template free functions and template member functions may use
// 'try' before their body (function-try-block).

int g_caught = 0;

// Template free function: function-try-block
template <typename T>
T safe_divide(T a, T b) try {
	if (b == (T)0)
		throw 42;
	return a / b;
} catch (int e) {
	g_caught = e;
	return (T)-1;
}

// Template member function: function-try-block
struct Box {
	int value;
	Box(int v) : value(v) {}

	template <typename T>
	int checked_add(T delta) try {
		if (delta < (T)0)
			throw -1;
		return value + static_cast<int>(delta);
	} catch (int e) {
		return e;
	}
};

int main() {
 // Template free function: normal path
	if (safe_divide(10, 2) != 5)
		return 1;

 // Template free function: exception path
	if (safe_divide(10, 0) != -1)
		return 2;
	if (g_caught != 42)
		return 3;

 // Template member function: normal path
	Box b(10);
	if (b.checked_add(5) != 15)
		return 4;

 // Template member function: exception path
	if (b.checked_add(-1) != -1)
		return 5;

	return 0;
}
