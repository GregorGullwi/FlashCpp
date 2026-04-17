struct Base {
	virtual ~Base() {}
};

struct Derived : Base {
	int payload = 7;
};

int main() {
	Derived derived;
	Base& base_ref = derived;

	const void* direct_expr_type = typeid(derived);
	const void* dynamic_type = typeid(base_ref);
	const void* exact_type = typeid(Derived);
	const void* static_type = typeid(Base);

	if (!direct_expr_type || !dynamic_type || !exact_type || !static_type) {
		return 1;
	}
	if (direct_expr_type != exact_type) {
		return 2;
	}
	if (dynamic_type != exact_type) {
		return 3;
	}
	if (dynamic_type == static_type) {
		return 4;
	}
	return 0;
}
