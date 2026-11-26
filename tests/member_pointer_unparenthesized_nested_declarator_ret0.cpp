// [dcl.mptr] keeps every preceding `*` on the pointee, so `int* C::*pm` is a
// data-member pointer whose pointee is `int*`. A nested-name-specifier owner
// (`nested::C::*`) is part of the same ptr-operator, in local, struct-member and
// namespace-scope declarations. `long**` and `double*` pointees make the pointer
// level and the pointee width both observable, and the parenthesized spelling is
// compared against the unparenthesized one.
namespace nested {
struct C {
	int* p;
	long** wide;
	double* exact;
};
}

int* nested::C::*global_pointer = &nested::C::p;
int* (nested::C::*global_parenthesized) = &nested::C::p;

struct Holder {
	int* nested::C::*pointer;
	long** nested::C::*wide;
	double* nested::C::*exact;
};

struct Plain {
	int* p;
};

struct PlainHolder {
	int* Plain::*pointer;
};

static_assert(sizeof(int* nested::C::*) == sizeof(int nested::C::*), "a nested owner keeps one member pointer level");

int main() {
	int value = 3;
	long wide_value = 77;
	long* wide_pointer = &wide_value;
	double exact_value = 4.0;
	nested::C object{&value, &wide_pointer, &exact_value};

	if (*(object.*global_pointer) != 3) {
		return 1;
	}
	if (*(object.*global_parenthesized) != 3) {
		return 2;
	}

	Holder holder{nullptr, nullptr, nullptr};
	holder.pointer = &nested::C::p;
	holder.wide = &nested::C::wide;
	holder.exact = &nested::C::exact;
	if (*(object.*(holder.pointer)) != 3) {
		return 3;
	}
	if (**(object.*(holder.wide)) != 77) {
		return 4;
	}
	if (*(object.*(holder.exact)) != 4.0) {
		return 5;
	}

	int other = 8;
	object.*global_pointer = &other;
	if (*(object.*(holder.pointer)) != 8) {
		return 6;
	}
	if (*(object.p) != 8) {
		return 7;
	}

	int* nested::C::*local_pointer = &nested::C::p;
	int* (nested::C::*local_parenthesized) = &nested::C::p;
	long** nested::C::*local_wide = &nested::C::wide;
	double* nested::C::*local_exact = &nested::C::exact;
	if (*(object.*local_pointer) != 8) {
		return 8;
	}
	if (*(object.*local_parenthesized) != 8) {
		return 9;
	}
	if (**(object.*local_wide) != 77) {
		return 10;
	}
	if (*(object.*local_exact) != 4.0) {
		return 11;
	}

	Plain plain{&value};
	PlainHolder plain_holder{nullptr};
	plain_holder.pointer = &Plain::p;
	int* Plain::*plain_local = &Plain::p;
	int* (Plain::*plain_parenthesized) = &Plain::p;
	if (*(plain.*(plain_holder.pointer)) != 3) {
		return 12;
	}
	if (*(plain.*plain_local) != 3) {
		return 13;
	}
	if (*(plain.*plain_parenthesized) != 3) {
		return 14;
	}

	int replacement = 41;
	plain.*(plain_holder.pointer) = &replacement;
	if (*(plain.*plain_local) != 41) {
		return 15;
	}
	if (*(plain.p) != 41) {
		return 16;
	}
	return 0;
}
