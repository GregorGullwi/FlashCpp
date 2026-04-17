// Test single-inheritance dynamic_cast
struct A {
int a_val;
A() : a_val(1) {}
virtual ~A() {}
virtual int getA() { return a_val; }
};

struct C : A {
int c_val;
C() : c_val(3) {}
virtual int getA() { return a_val + c_val; }
};

int main() {
C c;
A* ap = &c;

C* cp = dynamic_cast<C*>(ap);
if (cp) return cp->c_val;  // should return 3
return 0;
}
