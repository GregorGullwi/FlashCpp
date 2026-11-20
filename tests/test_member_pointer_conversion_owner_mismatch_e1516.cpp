// A pointer-to-member of a derived class cannot convert to a pointer-to-member
// of its base; the member-pointer conversion direction only allows base-to-
// derived.

struct D;
using DerivedMember = int D::*;

struct B {
	int value;
};

struct D : B {
	int other;
};

struct S {
	operator DerivedMember() const {
		return &D::other;
	}
};

int main() {
	int B::* pointer = S{};
	return pointer == nullptr ? 0 : 1;
}
