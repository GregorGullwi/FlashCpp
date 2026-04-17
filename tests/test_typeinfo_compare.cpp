#include <typeinfo>

struct Base {
virtual ~Base() {}
virtual int get() { return 1; }
};

struct Derived : Base {
virtual int get() { return 2; }
};

int main() {
Derived d;
Base* bp = &d;

// Test that typeid of polymorphic type returns actual type
bool same = (typeid(*bp) == typeid(Derived));
return same ? 2 : 0;  // return 2 if correct
}
