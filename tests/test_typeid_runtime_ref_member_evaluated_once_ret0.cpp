struct Base {
	virtual ~Base() {}
};

struct Derived : Base {};

struct Holder {
	Base& ref;
};

int g_get_holder_calls = 0;

Holder& getHolder(Holder& holder) {
	++g_get_holder_calls;
	return holder;
}

int main() {
	Derived derived;
	Holder holder{derived};

	const void* dynamic_type = typeid(getHolder(holder).ref);
	const void* exact_type = typeid(Derived);

	if (!dynamic_type || !exact_type) {
		return 1;
	}
	if (dynamic_type != exact_type) {
		return 2;
	}
	if (g_get_holder_calls != 1) {
		return 3;
	}

	return 0;
}
