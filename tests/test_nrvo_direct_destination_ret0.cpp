// A call that returns through the hidden return slot constructs directly in
// the caller's destination. The NRVO local's address is therefore the
// destination's address, no copy constructor runs, and the destination's
// destructor runs once at scope exit.

static void* constructed_address = nullptr;
static int destructor_count = 0;
static int copy_count = 0;

struct Big {
	long long value;
	long long extra;

	Big(int initial) : value(initial), extra(initial) {
		constructed_address = this;
	}

	Big(const Big& other) : value(other.value), extra(other.extra) {
		++copy_count;
	}

	~Big() {
		++destructor_count;
	}
};

Big make_big(int initial) {
	Big local(initial);
	local.value += 1;
	return local;
}

int main() {
	{
		Big result = make_big(41);
		if (result.value != 42 || result.extra != 41) {
			return 1;
		}
		if (&result != constructed_address) {
			return 2;
		}
		if (copy_count != 0) {
			return 3;
		}
	}
	if (destructor_count != 1) {
		return 4;
	}
	return 0;
}
