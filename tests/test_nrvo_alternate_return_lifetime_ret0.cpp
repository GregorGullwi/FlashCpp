// Named-local elision must be disabled when another return can execute while
// the candidate is alive. Here the nested Big(2) return would otherwise
// construct over a live Big local and skip that local's destructor.
//
// The alternate edge must still construct its result directly in the caller's
// hidden return slot, and the ordinary edge must keep NRVO.

extern "C" int printf(const char*, ...);

static int constructor_count = 0;
static int destructor_count = 0;
static int copy_count = 0;
static void* first_constructed_address = nullptr;
static void* last_constructed_address = nullptr;

struct Big {
	long long value;
	long long extra;

	Big(int initial) : value(initial), extra(initial) {
		++constructor_count;
		if (first_constructed_address == nullptr) {
			first_constructed_address = this;
		}
		last_constructed_address = this;
	}

	Big(const Big& other) : value(other.value), extra(other.extra) {
		++copy_count;
	}

	~Big() {
		++destructor_count;
	}
};

static Big choose(bool alternate) {
	Big local(1);
	if (alternate) {
		return Big(2);
	}
	return local;
}

static Big keep() {
	Big local(1);
	return local;
}

static void resetCounters() {
	constructor_count = 0;
	destructor_count = 0;
	copy_count = 0;
	first_constructed_address = nullptr;
	last_constructed_address = nullptr;
}

int main() {
	resetCounters();
	{
		Big result = choose(true);
		if (result.value != 2 || result.extra != 2) {
			return 1;
		}
		// The alternate result, not the named local, owns the destination.
		if (last_constructed_address != &result) {
			return 2;
		}
		if (first_constructed_address == &result) {
			return 3;
		}
		// The live local must be destroyed while leaving choose().
		if (destructor_count != 1) {
			printf("alternate destructors=%d\n", destructor_count);
			return 4;
		}
		if (constructor_count != 2 || copy_count != 0) {
			printf("alternate constructors=%d copies=%d\n", constructor_count, copy_count);
			return 5;
		}
	}
	if (destructor_count != 2) {
		return 6;
	}

	resetCounters();
	{
		Big result = keep();
		if (result.value != 1 || result.extra != 1) {
			return 7;
		}
		// A function without an intersecting exit keeps eliding its named local.
		if (first_constructed_address != &result || last_constructed_address != &result) {
			return 8;
		}
		if (constructor_count != 1 || copy_count != 0 || destructor_count != 0) {
			printf("normal constructors=%d copies=%d destructors=%d\n",
				constructor_count, copy_count, destructor_count);
			return 9;
		}
	}
	if (destructor_count != 1) {
		return 10;
	}
	return 0;
}
