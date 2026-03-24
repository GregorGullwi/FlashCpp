// Test: conversion operator returning an enum type via a type alias in a template.
// Exposes a bug in computeInstantiatedLookupName where the gTypeInfo lookup
// is gated on Type::Struct, so Type::Enum falls through and the stub is
// registered under the un-canonicalized name "operator value_type" instead
// of "operator Color".  This causes a name mismatch between the stub and
// the lazy registry key, leading to a missing conversion operator at codegen.
// Expected return: 0

enum Color { Red = 0, Green = 1, Blue = 2 };

template<typename T>
struct EnumWrapper {
	using value_type = T;
	T val_;
	EnumWrapper(T v) : val_(v) {}
	operator value_type() const { return val_; }
};

int main() {
	EnumWrapper<Color> w(Green);
	Color c = w;  // Should call operator Color() const => Green (1)
	if (c != Green) return 1;

	int x = c;  // Green == 1
	return x - 1;  // 0 if correct
}
