// A member-function-pointer type-id whose return type is a pointer keeps the
// parenthesized declarator group after that pointer: `int* (C::*)()`.
// sizeof, alignof and a C-style cast target are type-ids
// ([expr.sizeof], [expr.alignof], [expr.cast]/1), so they must consume the same
// abstract declarator as the alias and trait sites.
// Casting 0 to the type must produce a null member pointer, casting a real
// member-function address must keep the pointee distinct, and the pointer
// return must survive a call through the selected member.
struct C {
	int value;
	int* pointer;
	int* read_pointer() { return pointer; }
	int* read_nothing() { return nullptr; }
};

using PointerReturningMember = int* (C::*)();

int main() {
	if (sizeof(int* (C::*)()) != sizeof(PointerReturningMember)) {
		return 1;
	}
	if (sizeof(int* (C::*)()) != 8) {
		return 2;
	}
	if (alignof(int* (C::*)()) != alignof(PointerReturningMember)) {
		return 3;
	}

	int* (C::*null_member)() = (int* (C::*)())0;
	if (null_member != nullptr) {
		return 4;
	}
	if (!(null_member == nullptr)) {
		return 5;
	}

	int* (C::*nothing)() = (int* (C::*)())&C::read_nothing;
	C object{17, nullptr};
	if ((object.*nothing)() != nullptr) {
		return 6;
	}

	int* (C::*reader)() = (int* (C::*)())&C::read_pointer;
	int value = 41;
	object.pointer = &value;
	if ((object.*reader)() != &value) {
		return 7;
	}
	object.pointer = nullptr;
	if ((object.*reader)() != nullptr) {
		return 8;
	}
	return 0;
}
