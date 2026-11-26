// A qualified out-of-line static data member definition is one
// nested-name-specifier: the lookup uses the innermost owner, not the first
// token. Nested classes, namespaces, and a template-id owner of an explicitly
// specialized class template must each reach their own storage, including a
// parenthesized pointer-to-array declarator whose owner has template arguments.

struct Outer {
	struct Inner {
		static int data;
	};
};
int Outer::Inner::data = 11;

namespace N {
struct S {
	static int data;
};
}
int N::S::data = 22;

namespace Q {
template <typename T>
struct S {
	static int data;
	static int (*table)[2];
};
template <>
struct S<int> {
	static int data;
	static int (*table)[2];
};
}
int Q::S<int>::data = 33;
int rows[2] = {44, 55};
int (*Q::S<int>::table)[2] = &rows;

int main() {
	if (Outer::Inner::data != 11) {
		return 1;
	}
	if (N::S::data != 22) {
		return 2;
	}
	if (Q::S<int>::data != 33) {
		return 3;
	}
	if ((*Q::S<int>::table)[0] != 44 || (*Q::S<int>::table)[1] != 55) {
		return 4;
	}

	Outer::Inner::data = 66;
	N::S::data = 77;
	Q::S<int>::data = 88;
	if (Outer::Inner::data != 66) {
		return 5;
	}
	if (N::S::data != 77 || Q::S<int>::data != 88) {
		return 6;
	}
	return 0;
}
