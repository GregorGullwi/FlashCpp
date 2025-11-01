// Test: Member initialization with designated initializer
// C++20 feature: Designated initializers for member initialization

struct A {
	struct B {
		int a = 0;
	};

	B b = { .a = 1 };
};

int main() {
	A obj;
	return obj.b.a;  // Should return 1
}

