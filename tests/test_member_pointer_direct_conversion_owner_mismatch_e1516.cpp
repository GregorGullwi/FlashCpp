// A pointer-to-member of a derived class cannot convert to a pointer-to-member
// of its base; direct member-pointer initialization must reject the inverse
// direction.

struct A {
	int a;
};

struct B {
	int b;
};

struct D : A, B {
	int d;
};

int main() {
	int D::* derived_pointer = &D::d;
	int B::* base_pointer = derived_pointer;
	return base_pointer == nullptr ? 0 : 1;
}
