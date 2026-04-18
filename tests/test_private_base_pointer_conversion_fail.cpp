// Test that implicit Derived* -> PrivateBase* conversion is rejected.
// Per C++20 [conv.ptr]/3, the base class must be accessible.
// This test should NOT compile - it's expected to fail.

struct Base {
	int x;
};

struct Derived : private Base {
	int y;
};

int main() {
	Derived d;
	Base* bp = &d;  // ERROR: Base is a private base of Derived
	return bp->x;
}
