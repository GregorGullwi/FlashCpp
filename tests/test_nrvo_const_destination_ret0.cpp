// Top-level const on the initialized object must not disable elision. The
// deleted copy constructor proves the initializer cannot fall back to a copy.

struct Big {
	long long value;
	long long extra;

	Big(int initial) : value(initial), extra(initial) {
	}

	Big(const Big&) = delete;

	~Big() {
	}
};

static Big make() {
	return Big(42);
}

int main() {
	const Big result = make();
	return result.value == 42 && result.extra == 42 ? 0 : 1;
}
