// [dcl.mptr]: a member-pointer owner's nested-name-specifier may start at the
// global scope, so `int* ::N::C::*pm` is a pointer to a member of N::C whose
// type is int*. The owner spelling stays global-relative even inside another
// scope, and the same ptr-operator must work in a local declaration, in a
// parenthesized declarator, and in a type-id.
namespace N {
struct C {
	int* member;
	long* wide;
};
}

namespace Other {
int* ::N::C::*scoped_member = &N::C::member;
}

int first_value = 11;
long second_value = 23;

int* ::N::C::*global_member = &N::C::member;
long* ::N::C::*global_wide = &N::C::wide;

int main() {
	N::C object{nullptr, nullptr};
	object.member = &first_value;
	object.wide = &second_value;

	if (*(object.*global_member) != 11) {
		return 1;
	}
	if (*(object.*global_wide) != 23) {
		return 2;
	}
	if (object.*global_member != &first_value) {
		return 3;
	}
	if (*(object.*Other::scoped_member) != 11) {
		return 4;
	}

	long* ::N::C::*local_wide = &N::C::wide;
	if (*(object.*local_wide) != 23) {
		return 5;
	}
	int replacement = 37;
	object.*global_member = &replacement;
	if (*object.member != 37) {
		return 6;
	}

	int* (N::C::*parenthesized_member) = &N::C::member;
	if (*(object.*parenthesized_member) != 37) {
		return 7;
	}
	int* (::N::C::*parenthesized_global_member) = &N::C::member;
	if (*(object.*parenthesized_global_member) != 37) {
		return 8;
	}

	if (sizeof(int* ::N::C::*) != 8) {
		return 9;
	}
	if (sizeof(long* ::N::C::*) != 8) {
		return 10;
	}
	if (sizeof(int* (::N::C::*)) != 8) {
		return 11;
	}
	if (sizeof(object.*global_wide) != sizeof(long*)) {
		return 12;
	}
	return 0;
}
