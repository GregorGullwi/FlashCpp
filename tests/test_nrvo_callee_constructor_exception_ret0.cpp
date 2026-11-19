// A constructor that throws does not complete its object. Constructor tracking
// must keep both the callee's NRVO local and the caller's destination out of
// exception cleanup when construction never finishes.

static int destructor_count = 0;

struct Big {
	long long value;
	long long extra;

	Big(int initial) : value(initial), extra(initial) {
		throw 3;
	}

	~Big() {
		++destructor_count;
	}
};

Big make_fail() {
	Big local(1);
	return local;
}

int main() {
	try {
		Big result = make_fail();
		(void)result.value;
	} catch (...) {
	}
	return destructor_count == 0 ? 0 : 1;
}
