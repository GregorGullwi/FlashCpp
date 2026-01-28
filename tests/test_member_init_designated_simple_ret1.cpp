// Test: Simpler version - member initialization with brace initializer

struct B {
	int a = 0;
};

struct A {
	B b = { .a = 1 };
};

int main() {
	A obj;
	return obj.b.a;  // Should return 1
}

