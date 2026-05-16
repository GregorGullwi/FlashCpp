struct Base {
	virtual constexpr int value() const {
		return 0;
	}
};

struct Derived : Base {
	int x;

	constexpr Derived(int seed)
		: x(seed) {}

	constexpr int value() const override {
		return x;
	}
};

constexpr int eval_local_base_reference_dispatch() {
	Derived d(42);
	const Base& ref = d;
	return ref.value();
}

struct Counter {
	int v;

	constexpr Counter(int seed)
		: v(seed) {}

	constexpr void bump() {
		++v;
	}
};

constexpr int eval_local_struct_reference_mutation() {
	Counter c(41);
	Counter& ref = c;
	ref.bump();
	return c.v;
}

static_assert(eval_local_base_reference_dispatch() == 42);
static_assert(eval_local_struct_reference_mutation() == 42);

int main() {
	if (eval_local_base_reference_dispatch() != 42) {
		return 1;
	}
	if (eval_local_struct_reference_mutation() != 42) {
		return 2;
	}
	return 0;
}
