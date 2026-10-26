// Member access and member calls through a pointer to an intermediate base of a
// virtual-base diamond must adjust to the complete object's virtual base, not
// use the intermediate class's standalone virtual-base offset.

struct Root {
	long long value;
	long long read() const { return value; }
};

struct Left : virtual Root {
	int left;
};

struct Right : virtual Root {
	int right;
};

struct Diamond : Left, Right {
	short tail;
};

struct DirectBase {
	int value;
	int read() const { return value; }
};

struct DirectDerived : DirectBase {
	int tail;
};

int main() {
	Diamond diamond{};
	diamond.value = 41;
	diamond.left = 3;
	diamond.right = 5;
	diamond.tail = 7;
	Left* left = &diamond;
	Right* right = &diamond;
	if (diamond.value != 41)
		return 1;
	if (left->value != 41)
		return 2;
	if (left->read() != 41)
		return 3;
	if (right->value != 41)
		return 4;
	if (right->read() != 41)
		return 5;
	Left& left_reference = diamond;
	if (left_reference.value != 41)
		return 6;
	if (left_reference.read() != 41)
		return 7;
	if (diamond.read() != 41)
		return 8;

	DirectDerived direct{};
	direct.value = 43;
	direct.tail = 11;
	DirectDerived* direct_pointer = &direct;
	if (direct_pointer->read() != 43)
		return 9;
	if (direct_pointer->value != 43)
		return 10;
	return 0;
}
