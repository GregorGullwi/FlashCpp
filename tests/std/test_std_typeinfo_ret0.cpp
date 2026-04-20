// Test that <typeinfo> compiles and links correctly.
// Exercises: typeid operator (static and polymorphic), type_info::name().
//
// This validates the Itanium name-mangling for std::type_info vtable symbols
// (_ZNKSt9type_info10__do_catchE..., _ZNKSt9type_info11__do_upcastE...) because
// any mangling error would produce an undefined-reference link failure.
#include <typeinfo>

struct Base { virtual ~Base() {} };
struct Derived : Base { ~Derived() {} };

int main() {
	Derived d;
	Base* b = &d;

	const std::type_info& ti_base    = typeid(Base);
	const std::type_info& ti_derived = typeid(Derived);
	const std::type_info& ti_poly    = typeid(*b);  // dynamic dispatch — must resolve to Derived

	// type_info::name() must return a non-null pointer
	if (!ti_base.name()) return 1;
	if (!ti_derived.name()) return 2;
	if (!ti_poly.name()) return 3;

	return 0;
}
