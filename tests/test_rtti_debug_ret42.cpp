// Debug RTTI with multiple inheritance

struct A {
	int a_val;
	A() : a_val(1) {}
	virtual ~A() {}
};

struct B {
	int b_val;
	B() : b_val(2) {}
	virtual ~B() {}
};

struct C : A, B {
	int c_val;
	C() : c_val(3) {}
};

// External: print integer (helper)
extern "C" int puts(const char*);
extern "C" long int atol(const char*);

int main() {
	C c;
	A* ap = &c;

	// Try simple upcast first (C* to A*)
	// A* ap = &c already done

	// Now dynamic_cast back from A* to C*
	C* cp = dynamic_cast<C*>(ap);

	if (cp == &c) {
		return 42;  // Success
	}
	if (cp) {
		return 99;  // Got non-null but wrong pointer
	}
	return 0;  // Got nullptr
}
