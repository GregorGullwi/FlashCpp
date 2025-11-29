// Test: Member initialization with type name and designated initializer
// Form: B b2 = B{ .a = 2 };

struct B {
	int a = 0;
};

struct A {
	B b2 = B{ .a = 2 };
};

int main() {
	A obj;
	return obj.b2.a;  // Should return 2
}

