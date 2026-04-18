// Test dynamic_cast<void*> - returns pointer to most derived object

struct Base {
	int base_val;
	Base() : base_val(10) {}
	virtual ~Base() {}
};

struct Derived : Base {
	int derived_val;
	Derived() : derived_val(20) {
		base_val = 5;
	}
};

// Multiple inheritance: B is a non-primary base of Multi, so B* points
// into the middle of the Multi object.  dynamic_cast<void*> must use
// offset_to_top (vtable[-2]) to recover the most-derived object address.
struct A {
	int a;
	A() : a(1) {}
	virtual ~A() {}
};

struct B {
	int b;
	B() : b(2) {}
	virtual ~B() {}
};

struct Multi : A, B {
	int m;
	Multi() : m(3) {}
};

int main() {
	Derived d;
	Base* bp = &d;

	// Test 1: single-inheritance dynamic_cast<void*>
	void* vp = dynamic_cast<void*>(bp);
	if (vp != &d) return 1;

	// Test 2: multiple-inheritance dynamic_cast<void*> from non-primary base
	Multi obj;
	B* bp2 = &obj;  // bp2 points to B subobject (non-zero offset within obj)

	void* vp2 = dynamic_cast<void*>(bp2);
	if (vp2 != &obj) return 2;

	// Test 3: dynamic_cast<void*> on nullptr must return nullptr
	B* null_bp = nullptr;
	void* vp3 = dynamic_cast<void*>(null_bp);
	if (vp3 != nullptr) return 3;

	// Test 4: dynamic_cast<void*> on nullptr with single-inheritance base pointer
	Base* null_base = nullptr;
	void* vp4 = dynamic_cast<void*>(null_base);
	if (vp4 != nullptr) return 4;

	return 7;
}
