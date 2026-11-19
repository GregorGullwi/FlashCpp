// A return inside a __try body can execute while a named NRVO candidate is
// alive. The candidate scan must inspect SEH bodies so the alternate return
// does not construct over the live local.

static int destructor_count = 0;
static int constructor_count = 0;
static void* last_constructed = nullptr;

struct Big {
	long long value;
	long long extra;

	Big(int initial) : value(initial), extra(initial) {
		++constructor_count;
		last_constructed = this;
	}

	~Big() {
		++destructor_count;
	}
};

static Big choose(bool alternate) {
	Big local(1);
	__try {
		if (alternate) {
			return Big(2);
		}
	} __except (1) {
	}
	return local;
}

int main() {
	{
		Big result = choose(true);
		if (result.value != 2 || &result != last_constructed) {
			return 1;
		}
		if (destructor_count != 1 || constructor_count != 2) {
			return 2;
		}
	}
	if (destructor_count != 2) {
		return 3;
	}
	return 0;
}
