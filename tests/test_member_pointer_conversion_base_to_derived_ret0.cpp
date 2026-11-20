// A pointer-to-member of a base class converts to a pointer-to-member of a
// publicly derived class. The canonical member-pointer tail must keep that
// valid direction viable.

struct B;
struct D;
using BaseMember = int B::*;

struct B {
	int value;
};

struct D : B {};

struct S {
	operator BaseMember() const {
		return &B::value;
	}
};

int main() {
	int D::* pointer = S{};
	(void)pointer;
	return 0;
}
