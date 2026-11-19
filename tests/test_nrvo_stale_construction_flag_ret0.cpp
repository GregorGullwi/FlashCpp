// Construction flags are zeroed in the function prologue, so an exception
// thrown before a later declaration must not read a flag left by an earlier
// call in the same frame.

static int destructor_count = 0;

struct Big {
	long long value;
	long long extra;

	Big(int initial) : value(initial), extra(initial) {}

	~Big() {
		++destructor_count;
	}
};

static void run(bool should_throw) {
	try {
		if (should_throw) {
			throw 1;
		}
		Big local(7);
		(void)local.value;
	} catch (...) {
	}
}

int main() {
	run(false);
	run(true);
	return destructor_count == 1 ? 0 : 1;
}
