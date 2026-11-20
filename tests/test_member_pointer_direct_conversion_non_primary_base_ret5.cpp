// Direct member-object-pointer conversions from a base to a derived class
// adjust through the published base subobject. The same adjustment is needed
// for copy-initialization, return statements, and call arguments.

struct A {
	int a;
};

struct B {
	int b;
};

struct D : A, B {};

int D::* convert_return(int B::* pointer) {
	return pointer;
}

int read_through(D object, int D::* pointer) {
	return object.*pointer;
}

int main() {
	int B::* base_pointer = &B::b;
	int D::* copy_pointer = base_pointer;
	int D::* return_pointer = convert_return(base_pointer);

	D object;
	object.a = 1;
	object.b = 5;
	if (object.*copy_pointer != 5) {
		return 1;
	}
	if (object.*return_pointer != 5) {
		return 2;
	}
	if (read_through(object, base_pointer) != 5) {
		return 3;
	}
	return 5;
}
