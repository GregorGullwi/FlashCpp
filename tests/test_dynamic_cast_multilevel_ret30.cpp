struct Base {
	virtual ~Base() {}
};

struct Derived : Base {
	int derived = 10;
};

struct MoreDerived : Derived {
	int more = 20;
};

int main() {
	MoreDerived value;
	Base* base_ptr = &value;
	MoreDerived* more_ptr = dynamic_cast<MoreDerived*>(base_ptr);
	if (!more_ptr) {
		return 1;
	}
	return more_ptr->derived + more_ptr->more;
}
