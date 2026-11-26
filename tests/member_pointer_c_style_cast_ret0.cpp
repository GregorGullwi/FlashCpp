// A C-style cast target is a type-id ([expr.cast]/1), so it accepts the same
// member-pointer abstract-declarators as sizeof, alignof and trait operands.
// The cast must produce a usable member offset and keep a pointer pointee
// distinct from the plain pointee, so a target parsed as `int*` instead of
// `int S::*` reads the wrong storage or fails the null comparison.
struct S {
	int m;
	int* p;
	int read() const;
};

int S::read() const {
	return m;
}

int main() {
	S object{17, nullptr};
	int S::*null_member = (int S::*)0;
	if (null_member != nullptr) {
		return 1;
	}
	if (!(null_member == nullptr)) {
		return 2;
	}

	int S::*member = (int S::*)&S::m;
	if (object.*member != 17) {
		return 3;
	}
	object.*member = 41;
	if (object.m != 41) {
		return 4;
	}
	S* pointer = &object;
	pointer->*member = 11;
	if (object.m != 11) {
		return 5;
	}

	int value = 7;
	object.p = &value;
	int* S::*pointee = (int* S::*)&S::p;
	if (*(object.*pointee) != 7) {
		return 6;
	}
	*(object.*pointee) = 9;
	if (value != 9) {
		return 7;
	}
	if (object.*pointee != &value) {
		return 8;
	}

	int (S::*read)() const = (int (S::*)() const)&S::read;
	if ((object.*read)() != 11) {
		return 9;
	}
	int (S::*null_read)() const = (int (S::*)() const)0;
	if (null_read != nullptr) {
		return 10;
	}
	return 0;
}
