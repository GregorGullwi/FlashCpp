// Reference-to-array declarators in typedef, using-alias and abstract (type-id)
// spellings must parse and lower like the named form. The parenthesized
// `(&)[N...]` group carries the reference qualifier and array extents, and the
// alias must index through the referenced array.
typedef int (&IntRef3)[3];
typedef const int (&ConstIntRef2x2)[2][2];
using UsingRef3 = int (&)[3];
using UsingRef2x2 = int (&)[2][2];

int sum3(IntRef3 r) {
	return r[0] + r[1] + r[2];
}

int main() {
	int a[3] = {1, 2, 3};
	IntRef3 r = a;
	if (r[2] != 3) return 1;
	r[1] = 9;
	if (a[1] != 9) return 2;
	if (sum3(a) != 13) return 3;	// 1 + 9 + 3

	UsingRef3 u = a;
	u[0] = 5;
	if (a[0] != 5) return 4;

	int m[2][2] = {{1, 2}, {3, 4}};
	UsingRef2x2 um = m;
	if (um[1][1] != 4) return 5;
	um[0][0] = 7;
	if (m[0][0] != 7) return 6;

	ConstIntRef2x2 cm = m;
	if (cm[0][0] != 7) return 7;

	int (&cast_ref)[3] = static_cast<int (&)[3]>(a);
	if (cast_ref[2] != 3) return 8;
	return 0;
}
