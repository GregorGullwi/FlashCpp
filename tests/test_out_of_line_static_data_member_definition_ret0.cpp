// A namespace-scope definition of a class static data member completes the
// declaration the class already made; it must not create a second entity.
// Scalar, array, nested-class, namespace-qualified, and decayed-array forms are
// read and written at run time so a missing or duplicate storage symbol fails.

struct Plain {
	static int data;
	static int values[3];
};

int Plain::data;
int Plain::values[3];

struct Outer {
	struct Inner {
		static int data;
		static int values[3];
	};
};

int Outer::Inner::data;
int Outer::Inner::values[3];

namespace N {
struct S {
	static int data;
	static int values[3];
};
}

int N::S::data;
int N::S::values[3];

// Namespace-scope pointer initialization exercises array decay through the
// member's qualified storage symbol.
int* decayed = N::S::values;

int main() {
	Plain::data = 5;
	Plain::values[0] = 1;
	Plain::values[2] = 3;

	Outer::Inner::data = 7;
	Outer::Inner::values[1] = 8;

	N::S::data = 11;
	N::S::values[0] = 13;
	decayed[1] = 17;

	int* data_address = &Plain::data;
	*data_address += 4;

	int* local_decayed = Plain::values;
	local_decayed[1] = 19;
	if (Plain::values[1] != 19 || &local_decayed[1] != &Plain::values[1]) {
		return 1;
	}

	if (Plain::data != 9) {
		return 2;
	}
	if (Plain::values[0] != 1 || Plain::values[2] != 3) {
		return 3;
	}
	if (Outer::Inner::data != 7 || Outer::Inner::values[1] != 8) {
		return 4;
	}
	if (N::S::data != 11 || N::S::values[1] != 17) {
		return 5;
	}
	if (decayed != N::S::values) {
		return 6;
	}
	if (decayed[0] != 13) {
		return 7;
	}
	return 0;
}
