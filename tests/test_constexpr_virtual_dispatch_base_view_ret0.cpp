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

// --- char return type ---

struct BaseChar {
	virtual constexpr char get_char() const {
		return 'A';
	}
};

struct DerivedChar : BaseChar {
	char c;

	constexpr DerivedChar(char ch)
		: c(ch) {}

	constexpr char get_char() const override {
		return c;
	}
};

constexpr char eval_char_dispatch() {
	DerivedChar d('Z');
	const BaseChar* p = &d;
	return p->get_char();
}

static_assert(eval_char_dispatch() == 'Z');

// --- long long return type ---

struct BaseLL {
	virtual constexpr long long get_value() const {
		return 0LL;
	}
};

struct DerivedLL : BaseLL {
	long long v;

	constexpr DerivedLL(long long val)
		: v(val) {}

	constexpr long long get_value() const override {
		return v;
	}
};

constexpr long long eval_ll_dispatch() {
	DerivedLL d(123456789LL);
	const BaseLL* p = &d;
	return p->get_value();
}

static_assert(eval_ll_dispatch() == 123456789LL);

// --- this-based virtual dispatch (calling virtual through `this` inside a base method) ---

struct BaseIndirect {
	virtual constexpr int direct_value() const {
		return 0;
	}

	constexpr int indirect_value() const {
		return direct_value();
	}
};

struct DerivedIndirect : BaseIndirect {
	int x;

	constexpr DerivedIndirect(int v)
		: x(v) {}

	constexpr int direct_value() const override {
		return x;
	}
};

constexpr int eval_indirect_dispatch() {
	DerivedIndirect d(77);
	const BaseIndirect* p = &d;
	return p->indirect_value();
}

static_assert(eval_indirect_dispatch() == 77);

int main() {
	if (eval_reference_dispatch() != 42) {
		return 1;
	}
	if (eval_pointer_dispatch() != 19) {
		return 2;
	}
	if (eval_char_dispatch() != 'Z') {
		return 3;
	}
	if (eval_ll_dispatch() != 123456789LL) {
		return 4;
	}
	if (eval_indirect_dispatch() != 77) {
		return 5;
	}

	return 0;
}
