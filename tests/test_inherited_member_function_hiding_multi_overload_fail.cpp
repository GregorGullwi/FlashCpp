// Test that name hiding applies even when the derived class has multiple
// overloads of the same name.  When tryResolveConcreteMemberFunction returns
// nullptr due to ambiguity (2+ local overloads), the arity check must still
// reject calls that only match a hidden base-class overload.
//
// C++ [class.member.lookup]/1: if Derived declares any "getValue", all
// Base::getValue overloads are hidden unless re-introduced with a
// using-declaration.

struct Base {
	int getValue(int x) const { return x; }
};

struct Derived : Base {
	int getValue() const { return 42; }
	int getValue(int x, int y) const { return x + y; }
};

int main() {
	Derived d;
	// Base::getValue(int) is hidden by Derived's two overloads — this must fail.
	return d.getValue(10);
}
