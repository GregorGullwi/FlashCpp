// Regression test: rethrow (throw;) from inside a nested catch must only
// destroy the innermost catch's locals, not outer catch locals.

int g_outer_dtor = 0;
int g_inner_dtor = 0;

struct OuterGuard {
	~OuterGuard() {
		g_outer_dtor += 1;
	}
};

struct InnerGuard {
	~InnerGuard() {
		g_inner_dtor += 1;
	}
};

int main() {
	try {
		try {
			throw 1;
		} catch (int) {
			OuterGuard og;
			try {
				throw 2;
			} catch (int) {
				InnerGuard ig;
				throw;
			}
		}
	} catch (int) {
	}

	if (g_inner_dtor != 1) return 1;
	if (g_outer_dtor != 1) return 2;
	return 0;
}