// [dcl.init.aggr] disqualifies a class from being an aggregate when any base
// class is virtual, including a virtual base inherited indirectly. Treating
// such a class as an aggregate bypasses the most-derived constructor and
// leaves the runtime virtual-base table without its completed object offsets.

struct Root {
	long long value;
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

struct PlainAggregate {
	int value;
	long long wide;
};

template<class T>
struct Holder {
	T value;
};

struct HolderLeft : virtual Holder<long long> {
	int left;
};

struct HolderRight : virtual Holder<long long> {
	int right;
};

struct HolderDiamond : HolderLeft, HolderRight {
	short tail;
};

static_assert(!__is_aggregate(Left));
static_assert(!__is_aggregate(Right));
static_assert(!__is_aggregate(Diamond));
static_assert(!__is_aggregate(HolderDiamond));
static_assert(__is_aggregate(PlainAggregate));

Root* from_left(Left* value) {
	return value;
}

Root* from_right(Right* value) {
	return value;
}

Holder<long long>* from_holder_left(HolderLeft* value) {
	return value;
}

Holder<long long>* from_holder_right(HolderRight* value) {
	return value;
}

int main() {
	Diamond diamond{};
	diamond.value = 41;
	diamond.left = 3;
	diamond.right = 5;
	diamond.tail = 7;
	Root* left_root = from_left(&diamond);
	Root* right_root = from_right(&diamond);
	if (left_root != right_root)
		return 1;
	if (left_root->value != 41)
		return 2;
	if (diamond.value != 41)
		return 3;

	HolderDiamond holder{};
	holder.value = 43;
	holder.left = 11;
	holder.right = 13;
	holder.tail = 17;
	Holder<long long>* left_holder = from_holder_left(&holder);
	Holder<long long>* right_holder = from_holder_right(&holder);
	if (left_holder != right_holder)
		return 4;
	if (left_holder->value != 43)
		return 5;

	PlainAggregate plain{7, 9};
	if (plain.value != 7 || plain.wide != 9)
		return 6;
	return 0;
}
