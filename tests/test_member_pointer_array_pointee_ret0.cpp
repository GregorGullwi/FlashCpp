// A data-member pointer publishes its member's complete pointee, including
// array bounds. Address-of a member array therefore yields a pointer to the
// array for both an explicit ordered declarator and auto deduction, so the
// member pointer can be dereferenced and subscripted as an array.

struct C {
	int arr[3];
	int other;
};

struct Wide {
	long long values[2];
};

struct PointerMember {
	int* value;
};

int main() {
	C object{{10, 20, 30}, 99};

	int (C::*ordered)[3] = &C::arr;
	(object.*ordered)[0] = 11;
	(object.*ordered)[2] = 33;
	if (object.arr[0] != 11 || object.arr[2] != 33) {
		return 1;
	}
	if ((object.*ordered)[1] != 20) {
		return 2;
	}

	auto deduced = &C::arr;
	(object.*deduced)[1] = 22;
	if (object.arr[1] != 22) {
		return 3;
	}
	if ((object.*deduced)[0] != 11) {
		return 4;
	}

	int C::*scalar = &C::other;
	if (object.*scalar != 99) {
		return 5;
	}
	object.*scalar = 98;
	if (object.other != 98) {
		return 6;
	}

	Wide wide{{100, 200}};
	long long (Wide::*wide_ordered)[2] = &Wide::values;
	auto wide_deduced = &Wide::values;
	(wide.*wide_ordered)[1] = 250;
	if (wide.values[1] != 250) {
		return 7;
	}
	if ((wide.*wide_deduced)[0] != 100) {
		return 8;
	}

	// An ordinary pointer member pointer keeps its pointer pointee.
	PointerMember pointer_object{&object.other};
	int* (PointerMember::*pointer_member) = &PointerMember::value;
	if (*(pointer_object.*pointer_member) != 98) {
		return 9;
	}
	*(pointer_object.*pointer_member) = 97;
	if (object.other != 97) {
		return 10;
	}

	// Direct member access keeps its array shape.
	if (object.arr[2] != 33 || object.other != 97) {
		return 11;
	}
	return 0;
}
