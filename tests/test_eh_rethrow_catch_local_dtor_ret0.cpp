// Regression test: rethrowing from a catch body must unwind locals created
// inside that catch before control reaches the outer handler.

int g_destroyed = 0;

struct Guard {
	~Guard() {
		g_destroyed++;
	}
};

void rethrowWithCatchLocal() {
	try {
		throw 42;
	} catch (int) {
		Guard guard;
		throw;
	}
}

int main() {
	try {
		rethrowWithCatchLocal();
		return 1;
	} catch (int value) {
		if (value != 42)
			return 2;
		return g_destroyed == 1 ? 0 : 3;
	}

	return 4;
}