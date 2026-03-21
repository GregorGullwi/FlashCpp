// Regression: global/static compound assignment must yield a modifiable lvalue.
// C++20 [expr.ass]/7: E1 op= E2 yields an lvalue referring to E1.

int g_value = 1;

int main() {
	int result = 0;

	int* global_ptr = &(g_value += 2);
	if (global_ptr == &g_value && *global_ptr == 3) {
		result += 1;
	}

	static int s_value = 8;
	int* static_ptr = &(s_value -= 3);
	if (static_ptr == &s_value && *static_ptr == 5) {
		result += 10;
	}

	return result - 11;
}
