// The result constructed into the hidden return slot is fully constructed
// before local cleanup begins. If a later local destructor throws, the result
// must be destroyed during unwinding even though the caller never completes
// its initialization.

static int result_destructors = 0;
static int bomb_destructors = 0;

struct Big {
	long long value;
	long long extra;

	Big(int initial) : value(initial), extra(initial) {
	}

	~Big() {
		++result_destructors;
	}
};

struct Bomb {
	Bomb() {
	}

	~Bomb() noexcept(false) {
		++bomb_destructors;
		throw 3;
	}
};

static Big make() {
	return Big(42);
}

static Big relay() {
	{
		Bomb bomb;
		return make();
	}
}

int main() {
	try {
		Big result = relay();
		(void)result.value;
	} catch (int) {
	}
	if (bomb_destructors != 1 || result_destructors != 1) {
		return 1;
	}
	return 0;
}
