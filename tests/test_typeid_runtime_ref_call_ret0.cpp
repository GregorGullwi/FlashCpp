struct Base {
	virtual ~Base() {}
};

struct Derived : Base {};

Base& getBaseRef(Derived& derived) {
	return derived;
}

int main() {
	Derived derived;

	const void* runtime_type = typeid(getBaseRef(derived));
	const void* exact_type = typeid(Derived);
	const void* static_type = typeid(Base);

	if (!runtime_type || !exact_type || !static_type) {
		return 1;
	}
	if (runtime_type != exact_type) {
		return 2;
	}
	if (runtime_type == static_type) {
		return 3;
	}

	return 0;
}
