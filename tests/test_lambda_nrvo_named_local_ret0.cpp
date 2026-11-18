// A named local returned from a lambda call operator can be constructed directly
// in the hidden return slot. Its destructor then runs in the caller's scope, and
// no copy constructor is invoked at the return statement.

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

int main() {
	auto make = []() {
		Large local(42);
		local.value += 8;
		return local;
	};

	Large result = make();
	if (result.value != 50 || result.extra != 42) {
		return 1;
	}
	if (copy_count != 0 || destructor_count != 0) {
		printf("copies=%d destructors=%d\n", copy_count, destructor_count);
		return 2;
	}
	return 0;
}
