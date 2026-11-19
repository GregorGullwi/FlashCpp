// A forwarding "return make();" constructs in the caller's hidden return slot.
// When make() throws after constructing its NRVO local, the callee must destroy
// that object exactly once and the caller must not destroy the incomplete
// destination a second time.

static int good_destructors = 0;
static int bad_destructors = 0;
static int copy_count = 0;

struct Big {
	long long magic;
	long long extra;

	Big(int initial) : magic(initial), extra(1) {
	}

	Big(const Big& other) : magic(other.magic), extra(other.extra) {
		++copy_count;
	}

	~Big() {
		if (magic == 0x12345678) {
			++good_destructors;
		} else {
			++bad_destructors;
		}
	}
};

static Big make_throwing_big() {
	Big local(0x12345678);
	local.extra = 9;
	throw 5;
	return local;
}

static Big forward_throwing_big() {
	return make_throwing_big();
}

int main() {
	try {
		Big result = forward_throwing_big();
		(void)result.magic;
	} catch (int) {
	}
	if (good_destructors != 1 || bad_destructors != 0 || copy_count != 0) {
		return 1;
	}
	return 0;
}
