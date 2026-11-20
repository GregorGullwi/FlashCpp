// A pointer-to-member of a base class converts to a pointer-to-member of a
// publicly derived class. The canonical member-pointer tail must keep that
// valid direction viable, and the converted pointer must address the selected
// member with the member's own size.

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
	D object;
	object.value = 7;
	return object.*pointer;
}
