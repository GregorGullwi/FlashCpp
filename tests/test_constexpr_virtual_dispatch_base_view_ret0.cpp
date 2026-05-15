struct Base {
	virtual constexpr int value() const {
		return 1;
	}
};

struct Derived : Base {
	int x;

	constexpr Derived(int v)
		: x(v) {}

	constexpr int value() const override {
		return x;
	}
};

constexpr int call_through_reference(const Base& base) {
	return base.value();
}

constexpr int eval_reference_dispatch() {
	Derived d(42);
	return call_through_reference(d);
}

constexpr int eval_pointer_dispatch() {
	Derived d(19);
	const Base* base_ptr = &d;
	return base_ptr->value();
}

static_assert(eval_reference_dispatch() == 42);
static_assert(eval_pointer_dispatch() == 19);

int main() {
	if (eval_reference_dispatch() != 42) {
		return 1;
	}
	if (eval_pointer_dispatch() != 19) {
		return 2;
	}

	return 0;
}
