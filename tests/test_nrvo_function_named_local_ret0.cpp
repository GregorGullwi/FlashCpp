// A named local returned from a regular function can share the hidden return slot.
// The local's destructor is skipped on the successful return but remains registered
// for scope cleanup and exceptional exits.

extern "C" int printf(const char*, ...);

static int copy_count = 0;
static int destructor_count = 0;

struct Large {
	long long value;
	long long extra;

	Large(int initial) : value(initial), extra(initial) {}

	Large(const Large& other) : value(other.value), extra(other.extra) {
		++copy_count;
	}

	~Large() {
		++destructor_count;
	}
};

Large make_large(int initial) {
	Large local(initial);
	local.value += 8;
	return local;
}

int main() {
	Large result = make_large(42);
	if (result.value != 50 || result.extra != 42) {
		return 1;
	}
	if (copy_count != 0 || destructor_count != 0) {
		printf("copies=%d destructors=%d\n", copy_count, destructor_count);
		return 2;
	}
	return 0;
}
