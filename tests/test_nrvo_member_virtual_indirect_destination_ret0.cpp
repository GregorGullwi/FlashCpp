// Member, virtual, and indirect hidden-return calls construct directly in the
// caller destination, so the returned object's address is the destination's
// address.

static void* last_constructed = nullptr;

struct Big {
	long long value;
	long long extra;

	Big(int initial) : value(initial), extra(initial) {
		last_constructed = this;
	}

	~Big() {
	}
};

struct Base {
	virtual Big take(int initial) {
		return Big(initial + 1);
	}
};

struct Taker : Base {
	Big take(int initial) override {
		return Big(initial + 100);
	}
};

static Big make_big(int initial) {
	return Big(initial + 200);
}

int main() {
	Taker taker;

	Big member_result = taker.take(7);
	if (member_result.value != 107 || &member_result != last_constructed) {
		return 1;
	}

	Base& base = taker;
	Big virtual_result = base.take(9);
	if (virtual_result.value != 109 || &virtual_result != last_constructed) {
		return 2;
	}

	Big (*indirect)(int) = make_big;
	Big indirect_result = indirect(11);
	if (indirect_result.value != 211 || &indirect_result != last_constructed) {
		return 3;
	}
	return 0;
}
