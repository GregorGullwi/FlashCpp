// Test exception hierarchy matching: catch(Base&) should catch throw Derived{}
// This exercises __si_class_type_info (single-inheritance typeinfo) so that
// the personality routine can match the derived type to the base catch handler.

struct Base {
	virtual ~Base() {}
	int value = 1;
};

struct Derived : public Base {
	int extra = 2;
};

int main() {
	int result = 1;	// failure default
	try {
		throw Derived{};
	} catch (Base& b) {
		result = 0;	// success: caught as Base& through hierarchy
	}
	return result;
}
