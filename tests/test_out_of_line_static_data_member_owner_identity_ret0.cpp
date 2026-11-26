// Out-of-line owners with the same simple name in different namespaces must
// resolve to distinct class entities. Scalar and parenthesized pointer-to-array
// declarations exercise both the plain declarator-id path and the structural
// declarator path, and the values are written and read back through each owner.

namespace A {
struct S {
	static int data;
	static int (*table)[2];
};
}
namespace B {
struct S {
	static int data;
	static int (*table)[2];
};
}

int A::S::data = 1;
int B::S::data = 2;
int rows_a[2] = {3, 4};
int rows_b[2] = {5, 6};
int (*A::S::table)[2] = &rows_a;
int (*B::S::table)[2] = &rows_b;

int main() {
	if (A::S::data != 1 || B::S::data != 2) {
		return 1;
	}
	if ((*A::S::table)[1] != 4) {
		return 2;
	}
	if ((*B::S::table)[1] != 6) {
		return 3;
	}

	A::S::data = 11;
	B::S::data = 22;
	(*A::S::table)[0] = 33;
	(*B::S::table)[0] = 44;
	if (A::S::data != 11 || B::S::data != 22) {
		return 4;
	}
	if ((*A::S::table)[0] != 33 || (*B::S::table)[0] != 44) {
		return 5;
	}
	return 0;
}
