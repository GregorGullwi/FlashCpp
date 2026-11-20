// Member pointers to inherited members use the declaring-owner offset in every
// storage duration: global, static data member, constexpr, and local.

struct A {
	int a;
	int pad_a;
};

struct B {
	int pad_b;
	int b;
};

struct C3 {
	int c;
};

struct D : A, B, C3 {};

int B::* global_b = &D::b;

struct Holder {
	static int B::* static_b;
};

int B::* Holder::static_b = &D::b;
constexpr int C3::* constexpr_c = &D::c;

int main() {
	D object;
	object.a = 1;
	object.pad_a = 2;
	object.pad_b = 3;
	object.b = 4;
	object.c = 5;
	int B::* local_b = &D::b;
	if (object.*global_b != 4) {
		return 1;
	}
	if (object.*Holder::static_b != 4) {
		return 2;
	}
	if (object.*constexpr_c != 5) {
		return 3;
	}
	if (global_b != local_b) {
		return 4;
	}
	return 0;
}
