// A destructor clears its construction flag. A later loop iteration that
// throws before re-declaring the local must not destroy the already-destroyed
// object again.

static int destructor_count = 0;

struct Big {
	long long value;
	long long extra;

	Big(int initial) : value(initial), extra(initial) {}

	~Big() {
		++destructor_count;
	}
};

static void maybe_throw(bool should_throw) {
	if (should_throw) {
		throw 1;
	}
}

int main() {
	for (int iteration = 0; iteration < 2; ++iteration) {
		try {
			maybe_throw(iteration == 1);
			Big local(7);
			(void)local.value;
		} catch (...) {
		}
	}
	return destructor_count == 1 ? 0 : 1;
}
