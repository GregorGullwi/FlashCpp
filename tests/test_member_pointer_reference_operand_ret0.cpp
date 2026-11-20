// Reference operands to a member-pointer access keep their member-pointer
// identity, including reference parameters.

struct C {
	int x;
};

int read_member(C& object, int C::*& pointer) {
	return object.*pointer;
}

int read_const_member(C& object, int C::* const& pointer) {
	return object.*pointer;
}

int main() {
	C object;
	object.x = 7;
	int C::* pointer = &C::x;
	if (read_member(object, pointer) != 7) {
		return 1;
	}
	if (read_const_member(object, pointer) != 7) {
		return 2;
	}
	return 0;
}
