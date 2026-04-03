struct Pair {
	int a;
	int b;
};

static Pair g_pair{30, 40};

struct Base {
	int value;

	Base(int v = 0) : value(v) {}

	virtual int& getRef() {
		return value;
	}

	virtual Pair&& getRvalueRef() {
		return static_cast<Pair&&>(g_pair);
	}
};

struct Derived : Base {
	int extra;

	Derived(int v, int e) : Base(v), extra(e) {}

	int& getRef() override {
		return extra;
	}

	Pair&& getRvalueRef() override {
		return static_cast<Pair&&>(g_pair);
	}
};

int main() {
	Derived d(10, 20);
	Base* ptr = &d;
	Base& ref = d;

	int& from_ptr = ptr->getRef();
	int& from_ref = ref.getRef();
	from_ptr += 1;
	from_ref += 2;
	if (d.value != 10 || d.extra != 23 || from_ptr != 23 || from_ref != 23) {
		return 1;
	}

	Pair&& rv_from_ptr = ptr->getRvalueRef();
	Pair&& rv_from_ref = ref.getRvalueRef();
	rv_from_ptr.a += 5;
	rv_from_ref.b += 7;
	if (g_pair.a != 35 || g_pair.b != 47 || rv_from_ptr.a != 35 || rv_from_ref.b != 47) {
		return 2;
	}

	return 0;
}
