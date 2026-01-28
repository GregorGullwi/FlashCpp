// Test: Nested struct with member initialization
// Inner struct's default initializers should be used

struct Inner {
	int a = 5;
};

struct Outer {
	Inner inner;
	int b = 10;
};

int main() {
	Outer o;
	return o.inner.a + o.b;  // Should return 15 (5 + 10)
}

