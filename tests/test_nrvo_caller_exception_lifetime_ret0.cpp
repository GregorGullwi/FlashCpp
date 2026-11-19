// A caller destination is constructed through the hidden return slot. When the
// callee throws after constructing its NRVO local, exception cleanup destroys
// that object once in the callee and must not destroy the caller destination a
// second time while its initialization is incomplete.

static int destructor_count = 0;
static int copy_count = 0;

struct Big {
	long long value;
	long long extra;

	Big(int initial) : value(initial), extra(initial) {}

	Big(const Big& other) : value(other.value), extra(other.extra) {
		++copy_count;
	}

	~Big() {
		++destructor_count;
	}
};

Big make_fail(int initial) {
	Big local(initial);
	local.value += 1;
	throw 7;
	return local;
}

Big make_ok(int initial) {
	Big local(initial);
	local.value += 1;
	return local;
}

int main() {
	try {
		Big failed = make_fail(1);
		(void)failed.value;
	} catch (...) {
	}
	if (destructor_count != 1 || copy_count != 0) {
		return 1;
	}

	try {
		Big completed = make_ok(5);
		(void)completed.value;
		throw 8;
	} catch (...) {
	}
	if (destructor_count != 2 || copy_count != 0) {
		return 2;
	}
	return 0;
}
