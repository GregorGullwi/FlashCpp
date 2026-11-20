// Dependent member-pointer parameters and conditional member pointers keep
// enough type identity for a later access.

struct C {
	int x;
};

template <class T>
int read_member(C& object, int T::* pointer) {
	return object.*pointer;
}

template <class T>
auto address_of(T C::* pointer) {
	return pointer;
}

int main() {
	C object;
	object.x = 9;
	int C::* pointer = &C::x;
	if (read_member<C>(object, pointer) != 9) {
		return 1;
	}
	auto copied = address_of<int>(&C::x);
	if (object.*copied != 9) {
		return 2;
	}
	auto conditional = true ? &C::x : nullptr;
	if (object.*conditional != 9) {
		return 3;
	}
	return 0;
}
