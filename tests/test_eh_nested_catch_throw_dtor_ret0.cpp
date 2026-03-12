// Regression test: throw <expr> from inside a nested catch must only destroy
// the innermost catch's locals, not outer catch locals (which are still live).
// Using min_element instead of back() on catch_scope_base_depth_stack_ would
// over-destroy, causing double-destruction of the outer catch's Guard.

int g_dtor_count = 0;

struct Guard {
	int id;
	Guard(int i) {
		id = i;
	}
	~Guard() {
		g_dtor_count += id;
	}
};

int main() {
	try {
		try {
			throw 1;
		} catch (int) {
			Guard outer_guard(100);
			try {
				throw 2;
			} catch (int) {
				Guard inner_guard(1);
				throw 3;
			}
		}
	} catch (int) {
	}

	// Correct: inner_guard dtor (1) + outer_guard dtor (100) = 101
	// min_element bug: inner_guard (1) + outer_guard (100) + outer_guard again (100) = 201
	if (g_dtor_count != 101) return 1;
	return 0;
}
