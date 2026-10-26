// A member-function pointer whose canonical owner is a base of the object
// adjusts the implicit this pointer before the indirect call, including a
// non-primary non-virtual base and a virtual base.

struct Primary {
	int first;
};

struct Secondary {
	int value;
	int add(int amount) {
		return value + amount;
	}
};

struct Derived : Primary, Secondary {};

struct VirtualBase {
	int value;
	int add(int amount) {
		return value + amount;
	}
};

struct VirtualDerived : virtual VirtualBase {};

int main() {
	Derived derived;
	derived.first = 1;
	derived.value = 40;
	int (Secondary::*add)(int) = &Secondary::add;
	if ((derived.*add)(2) != 42) {
		return 1;
	}
	Derived* pointer = &derived;
	if ((pointer->*add)(1) != 41) {
		return 2;
	}
	VirtualDerived virtual_derived;
	virtual_derived.value = 40;
	int (VirtualBase::*virtual_add)(int) = &VirtualBase::add;
	if ((virtual_derived.*virtual_add)(2) != 42) {
		return 3;
	}
	return 0;
}
