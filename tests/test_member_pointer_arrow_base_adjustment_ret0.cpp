// Arrow member-pointer access and this->* adjust the object address to the
// member pointer's declared owner, including non-primary bases.

struct A {
	int a;
};

struct B {
	int pad;
	int b;
};

struct D : A, B {
	int via_this() {
		int B::* pointer = &B::b;
		return this->*pointer;
	}
};

int main() {
	D object;
	object.a = 1;
	object.pad = 2;
	object.b = 22;

	int B::* pointer = &B::b;
	D* object_pointer = &object;
	if (object_pointer->*pointer != 22) {
		return 1;
	}
	if (object.via_this() != 22) {
		return 2;
	}
	return 0;
}
