// A member-pointer declarator owns the `C::*` ptr-operator before the
// declarator-id; an out-of-line definition keeps `C::name` for the name. The
// pointer-pointee declarator must not steal `int* C::data` or `int* C::f()`,
// including a nested-name-specifier owner.
int first_value = 13;
int second_value = 29;

struct C {
	static int* data;
	static int* f();
	int* p;
	int* g();
};

int* C::data = &first_value;

int* C::f() {
	return C::data;
}

int* C::g() {
	return nullptr;
}

namespace nested {
struct D {
	static int* data;
	static int* f();
	int* p;
	int* g();
};
}

int* nested::D::data = &second_value;

int* nested::D::f() {
	return nested::D::data;
}

int* nested::D::g() {
	return nullptr;
}

int main() {
	int value = 5;
	C object;
	object.p = &value;
	if (*C::f() != 13) {
		return 1;
	}
	if (C::data != &first_value) {
		return 2;
	}
	if (object.g() != nullptr) {
		return 3;
	}
	int* C::*pointer = &C::p;
	if (*(object.*pointer) != 5) {
		return 4;
	}

	nested::D nested_object;
	nested_object.p = &value;
	if (*nested::D::f() != 29) {
		return 5;
	}
	if (nested::D::data != &second_value) {
		return 6;
	}
	if (nested_object.g() != nullptr) {
		return 7;
	}
	int* nested::D::*nested_pointer = &nested::D::p;
	if (*(nested_object.*nested_pointer) != 5) {
		return 8;
	}

	int replacement = 23;
	object.*pointer = &replacement;
	if (*(object.*pointer) != 23) {
		return 9;
	}
	nested_object.*nested_pointer = &replacement;
	if (*(nested_object.*nested_pointer) != 23) {
		return 10;
	}
	if (*C::f() != 13 || *nested::D::f() != 29) {
		return 11;
	}
	return 0;
}
