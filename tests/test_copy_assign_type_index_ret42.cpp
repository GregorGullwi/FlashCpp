// Test that copy/move assignment operator detection correctly checks type_index
// Verifies that Foo::operator=(const Bar&) is NOT treated as Foo's copy assignment

struct Bar {
	int value;
};

struct Foo {
	int data;
	// This is NOT a copy assignment for Foo (wrong type)
	Foo& operator=(const Bar& other) {
		data = other.value + 10;
		return *this;
	}
	// This IS the copy assignment for Foo
	Foo& operator=(const Foo& other) {
		data = other.data;
		return *this;
	}
};

int main() {
	Foo f;
	f.data = 0;
	Foo g;
	g.data = 42;
	f = g;  // Should use copy assignment from Foo
	return f.data;  // Should return 42
}
