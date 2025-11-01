// Test: Member initialization with various initializer forms
// Tests designated initializers, brace initializers, and constructor calls

struct A {
	struct B {
		int a = 0;
	};

	B b1 = { .a = 1 };
	B b2 = B{ .a = 2 };
	B b3 = B{ 3 };

	struct C {
		C() = default;
		C(int new_a) : a(new_a) {}
		int a = 0;
	};

	C c1{ 1 };
	C c2 = C(2);
	C c3 = C{3};
};

int main() {
	A obj;
	// b1.a = 1, b2.a = 2, b3.a = 3
	// c1.a = 1, c2.a = 2, c3.a = 3
	// Total: 1 + 2 + 3 + 1 + 2 + 3 = 12
	return obj.b1.a + obj.b2.a + obj.b3.a + obj.c1.a + obj.c2.a + obj.c3.a;
}

