// Test dynamic_cast with multiple inheritance and non-primary base adjustment

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
C() : c_val(3) {
a_val = 10;
b_val = 20;
}
};

int main() {
	C c;

	// dynamic_cast from A* to C* (should succeed)
	A* ap = &c;
	C* cp = dynamic_cast<C*>(ap);
	if (!cp) return 1;

	// Static upcast to a non-primary base must apply the B subobject offset.
	// dynamic_cast from B* to C* (should succeed - cross-cast through multiple inheritance)
	B* bp = &c;
	B* expected_bp = static_cast<B*>(&c);
	if (bp != expected_bp) return 2;
	C* cp2 = dynamic_cast<C*>(bp);
	if (!cp2) return 3;

	// dynamic_cast from A* to B* (cross-cast should succeed via C)
	B* bp2 = dynamic_cast<B*>(ap);
	if (!bp2) return 4;
	if (bp2 != expected_bp) return 5;

	return bp2->b_val + cp->c_val + cp2->c_val; // 20 + 3 + 3 = 26
}
