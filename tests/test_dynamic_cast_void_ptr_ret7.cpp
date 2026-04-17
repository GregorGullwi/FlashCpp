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

int main() {
Derived d;
Base* bp = &d;

// dynamic_cast<void*> should return the most-derived object address
void* vp = dynamic_cast<void*>(bp);

// For a simple single-inheritance case, the most-derived object IS the same as Base subobject
// when there's no virtual base
if (vp == &d) {
return 7;
}
return 0;
}
