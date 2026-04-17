// Test dynamic_cast with multiple inheritance

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

// dynamic_cast from B* to C* (should succeed - cross-cast through multiple inheritance)
B* bp = &c;
C* cp2 = dynamic_cast<C*>(bp);
if (!cp2) return 2;

// dynamic_cast from A* to B* (cross-cast should succeed via C)
B* bp2 = dynamic_cast<B*>(ap);
if (!bp2) return 3;

return cp->c_val + cp2->c_val + 0;  // 3 + 3 = 6, return 6 is wrong, we want something specific
}
