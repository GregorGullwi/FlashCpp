// Direct member-object-pointer assignment and explicit static_cast adjust the
// base-subobject offset exactly once.

struct A {
	int a;
};

struct B {
	int pad;
	int b;
};

struct D : A, B {};

int main() {
	D object;
	object.a = 1;
	object.pad = 2;
	object.b = 3;

	int B::* base_pointer = &D::b;
	int D::* assigned = nullptr;
	assigned = base_pointer;
	if (object.*assigned != 3) {
		return 1;
	}

	int D::* initialized = nullptr;
	initialized = &D::b;
	if (object.*initialized != 3) {
		return 2;
	}

	int D::* casted = static_cast<int D::*>(base_pointer);
	if (object.*casted != 3) {
		return 3;
	}
	return 0;
}
