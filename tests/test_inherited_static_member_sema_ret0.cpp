// Test that sema can infer the type of an inherited static member accessed
// via dot notation on a derived-class object.  Before the fix, the static
// member loop in inferExpressionType(MemberAccessNode) only checked the
// immediate struct, so inherited static members returned an empty type and
// sema could not annotate implicit conversions involving them.
//
// NOTE: codegen resolves member access types independently, so this test
// exercises correctness but may not fail at runtime even without the sema
// fix.  It serves as a regression test for the sema type-inference path
// and ensures the end-to-end behavior is correct for inherited static
// member access via dot notation.

struct Base {
	static int shared_value;
};
int Base::shared_value = 42;

struct Derived : Base {
	int local;
};

// Use the inherited static member in an arithmetic expression.
// d.shared_value is Base::shared_value (int, value 42).
int use_inherited_static(Derived& d) {
	short s = 8;
	return d.shared_value + s;
}

int main() {
	Derived d;
	d.local = 0;
	int result = use_inherited_static(d);
	// result should be 50; return 50 - 50 = 0 on success.
	return result - 50;
}
