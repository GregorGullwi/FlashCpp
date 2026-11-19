// A member-call initializer must not arm the caller destination while lowering
// a same-type argument call. The argument is its own temporary, not the
// destination object.

static void* first_constructed = nullptr;

struct Big {
	long long value;
	long long extra;

	Big(int initial) : value(initial), extra(initial) {
		if (first_constructed == nullptr) {
			first_constructed = this;
		}
	}
};

static Big make_big(int initial) {
	return Big(initial);
}

struct Taker {
	long long padding;

	Big take(Big argument) {
		return Big(argument.value + 100);
	}
};

int main() {
	Taker taker{0};
	Big result = taker.take(make_big(7));
	if (result.value != 107) {
		return 1;
	}
	if (first_constructed == &result) {
		return 2;
	}
	return 0;
}
