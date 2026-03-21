// Regression: simple global/static assignment must yield a modifiable lvalue.
// C++20 [expr.ass]/3: the result of E1 = E2 is an lvalue referring to E1.

int g_value = 0;

int main() {
	int result = 0;

	int* global_ptr = &(g_value = 1);
	if (global_ptr == &g_value && *global_ptr == 1) {
		result += 1;
	}

	static int s_value = 0;
	int* static_ptr = &(s_value = 4);
	if (static_ptr == &s_value && *static_ptr == 4) {
		result += 10;
	}

	return result - 11;
}
