// Regression test: std::type_info back-substitution in Itanium name mangling.
//
// Before the fix, the vtable slots for std::type_info virtual methods used
// un-substituted Itanium symbols, causing undefined references when linking:
//   __do_catch:  _ZNKSt9type_info10__do_catchEPKSt9type_infoPPvj   (wrong)
//   __do_upcast: _ZNKSt9type_info11__do_upcastEPK10__cxxabiv117__class_type_infoPPv (wrong)
//
// After the fix, the ABI-correct back-reference forms are emitted:
//   __do_catch:  _ZNKSt9type_info10__do_catchEPKS_PPvj             (S_ = std::type_info)
//   __do_upcast: _ZNKSt9type_info11__do_upcastEPKN10__cxxabiv117__class_type_infoEPPv
//
// This file must compile and link successfully (was previously a link error).
// It also exercises the Itanium N...E wrapping fix for multi-component types:
//   __do_upcast parameter __cxxabiv1::__class_type_info  ->  N10__cxxabiv117__class_type_infoE
#include <typeinfo>

struct Animal {
	virtual ~Animal() {}
	virtual int sound() const { return 1; }
};

struct Dog : Animal {
	~Dog() {}
	int sound() const override { return 2; }
};

int main() {
	Dog dog;
	Animal* a = &dog;

	// These typeid calls force emission of the std::type_info vtable,
	// which must reference the correctly-named __do_catch and __do_upcast
	// symbols in libstdc++.
	(void)typeid(Animal);
	(void)typeid(Dog);
	(void)typeid(*a);

	// Virtual dispatch on the user-defined classes must also work.
	if (a->sound() != 2) return 1;

	return 0;
}
