// Test: Different forms of member initialization

struct B {
	int a = 0;
};

struct A {
	B b1 = { .a = 1 };  // Designated initializer
};

int main() {
	A obj;
	return obj.b1.a;  // Should return 1
}

