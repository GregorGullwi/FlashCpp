// Exercises finalized virtual-base ABI facts through the existing adjustment
// lowering for ordinary and class-template virtual inheritance, with a
// nonzero-offset non-virtual base as the control.

struct Root {
	long long value;
	Root(long long initial) : value(initial) {}
};

struct Left : virtual Root {
	int left;
	Left(long long initial, int left_value) : Root(initial), left(left_value) {}
};

struct Right : virtual Root {
	int right;
	Right(long long initial, int right_value) : Root(initial), right(right_value) {}
};

struct Diamond : Left, Right {
	short tail;
	Diamond(long long initial, int left_value, int right_value, short tail_value)
		: Root(initial), Left(initial, left_value), Right(initial, right_value), tail(tail_value) {}
};

template<class T>
struct Holder {
	T value;
	Holder() : value(43) {}
};

struct LeftHolder : virtual Holder<long long> {
	int left;
};

struct RightHolder : virtual Holder<long long> {
	int right;
};

struct HolderDiamond : LeftHolder, RightHolder {
	short tail;
};

struct Padding {
	char byte;
};

struct Plain {
	long long value;
};

struct NonVirtual : Padding, Plain {
	int tag;
};

Root* as_root(Left* value) {
	return value;
}

Root* as_root(Right* value) {
	return value;
}

long long read_root(Root* value) {
	return value->value;
}

Holder<long long>* as_holder(LeftHolder* value) {
	return value;
}

Holder<long long>* as_holder(RightHolder* value) {
	return value;
}

long long read_holder(Holder<long long>* value) {
	return value->value;
}

long long read_plain(Plain* value) {
	return value->value;
}

int main() {
	Diamond diamond(41, 3, 5, 7);
	Root* left_root = as_root(static_cast<Left*>(&diamond));
	Root* right_root = as_root(static_cast<Right*>(&diamond));
	if (left_root != right_root)
		return 1;
	if (read_root(left_root) != 41)
		return 2;
	if (diamond.left + diamond.right + diamond.tail != 15)
		return 3;

	HolderDiamond holder;
	holder.left = 11;
	holder.right = 13;
	holder.tail = 17;
	Holder<long long>* left_holder = as_holder(static_cast<LeftHolder*>(&holder));
	Holder<long long>* right_holder = as_holder(static_cast<RightHolder*>(&holder));
	if (left_holder != right_holder)
		return 4;
	if (read_holder(left_holder) != 43)
		return 5;
	if (holder.left + holder.right + holder.tail != 41)
		return 6;

	NonVirtual nonvirtual{};
	nonvirtual.value = 47;
	nonvirtual.tag = 19;
	Plain* plain = &nonvirtual;
	if (read_plain(plain) != 47)
		return 7;
	if (plain == nullptr)
		return 8;
	return 0;
}
