// Test that a derived-class member function hides all base-class overloads of
// the same name, even those with different signatures.  Calling the hidden
// base overload through the derived type must be a compile error.
//
// C++ [class.member.lookup]/1: if Derived declares any "getValue", all
// Base::getValue overloads are hidden unless re-introduced with a
// using-declaration.

struct Base {
	int getValue(int x) const { return x; }
};

struct Derived : Base {
	int getValue() const { return 42; }
};

int main() {
	Derived d;
	// Base::getValue(int) is hidden by Derived::getValue() — this must fail.
	return d.getValue(10);
}
