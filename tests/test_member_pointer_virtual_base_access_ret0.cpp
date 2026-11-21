// Access a B-owned data member through a D receiver. D reaches B through a
// public virtual V anchor, then a nonzero ordinary V-to-B base offset.
struct Padding {
	int pad;
};

struct B {
	int value;
};

struct V : Padding, B {};
struct D : virtual V {};
struct Extra {
	long long pad;
};
struct MostDerived : D, Extra {};

int main() {
	MostDerived object;
	object.value = 42;
	int B::* member = &B::value;
	D& receiver = object;
	D* pointer = &receiver;
	D*& pointer_reference = pointer;
	if (receiver.*member != 42) {
		return 1;
	}
	if (pointer_reference->*member != 42) {
		return 2;
	}
	pointer_reference->*member = 23;
	return object.value == 23 ? 0 : 3;
}
