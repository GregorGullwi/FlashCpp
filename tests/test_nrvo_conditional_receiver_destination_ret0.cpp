// A conditional receiver selects one call target at runtime. The selected call
// must still construct its result in the caller destination.

static int constructor_count = 0;
static int destructor_count = 0;
static int copy_count = 0;
static void* constructed_address = nullptr;

struct Big {
	long long value;
	long long extra;

	Big(int initial) : value(initial), extra(initial) {
		++constructor_count;
		constructed_address = this;
	}

	Big(const Big& other) : value(other.value), extra(other.extra) {
		++copy_count;
	}

	~Big() {
		++destructor_count;
	}
};

struct Taker {
	long long padding;

	Big take(int initial) {
		return Big(initial + 100);
	}
};

static Big choose_receiver(bool first, Taker& a, Taker& b) {
	return (first ? a : b).take(7);
}

int main() {
	Taker a{0};
	Taker b{0};
	{
		Big result = choose_receiver(true, a, b);
		if (result.value != 107 || constructed_address != &result) {
			return 1;
		}
		if (constructor_count != 1 || copy_count != 0 || destructor_count != 0) {
			return 2;
		}
	}
	if (destructor_count != 1) {
		return 3;
	}

	constructor_count = 0;
	destructor_count = 0;
	copy_count = 0;
	constructed_address = nullptr;
	Taker initializer_a{0};
	Taker initializer_b{0};
	{
		Big result = (false ? initializer_a : initializer_b).take(9);
		if (result.value != 109 || constructed_address != &result) {
			return 4;
		}
		if (constructor_count != 1 || copy_count != 0 || destructor_count != 0) {
			return 5;
		}
	}
	return destructor_count == 1 ? 0 : 6;
}
