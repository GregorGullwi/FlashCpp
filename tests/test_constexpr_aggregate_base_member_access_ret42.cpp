// Constexpr member access through an aggregate base subobject must read the
// materialized base value. Reads must work at base offset zero, through a
// non-primary base placed after padding, with several base members, and through
// a base chain that is two levels deep. A base member that is itself a record or
// an array must keep its nested value, a constexpr function must see the same
// value, and the runtime object must agree with the constant expression.

struct FirstBase {
	int first;
	int second;
};

struct Padded {
	long long guard;
};

struct Derived : FirstBase {
	int tail;
};

struct Multi : Padded, FirstBase {
	short tag;
};

struct Mid : FirstBase {
	int middle;
};

struct Leaf : Mid {
	short last;
};

struct Point {
	int x;
	int y;
};

struct RecordBase {
	Point point;
	int values[2];
};

struct RecordDerived : RecordBase {
	int tail;
};

constexpr Derived derived{1, 2, 3};
static_assert(derived.first == 1);
static_assert(derived.second == 2);
static_assert(derived.tail == 3);
static_assert(derived.first + derived.second + derived.tail == 6);

constexpr Multi multi{{4000000000LL}, {4, 5}, 6};
static_assert(multi.guard == 4000000000LL);
static_assert(multi.first == 4);
static_assert(multi.second == 5);
static_assert(multi.tag == 6);

constexpr Leaf leaf{7, 8, 9, 10};
static_assert(leaf.first == 7);
static_assert(leaf.second == 8);
static_assert(leaf.middle == 9);
static_assert(leaf.last == 10);

constexpr RecordDerived record_derived{{{11, 12}, {13, 14}}, 15};
static_assert(record_derived.point.x == 11);
static_assert(record_derived.point.y == 12);
static_assert(record_derived.values[0] == 13);
static_assert(record_derived.values[1] == 14);
static_assert(record_derived.tail == 15);

constexpr Derived aggregate_values[2] = {{{16, 17}, 18}, {{19, 20}, 21}};
static_assert(aggregate_values[0].first == 16);
static_assert(aggregate_values[1].second == 20);
static_assert(aggregate_values[1].tail == 21);

constexpr int read_second(Derived value) {
	return value.second;
}

constexpr int read_first(Leaf value) {
	return value.first;
}

constexpr int read_middle(Leaf value) {
	return value.middle;
}

static_assert(read_second(derived) == 2);
static_assert(read_first(leaf) == 7);
static_assert(read_middle(leaf) == 9);

int main() {
	Derived runtime{21, 22, 23};
	if (runtime.first != 21) {
		return 1;
	}
	if (runtime.second != 22) {
		return 2;
	}
	if (runtime.tail != 23) {
		return 3;
	}

	Multi runtime_multi{{4000000000LL}, {24, 25}, 26};
	if (runtime_multi.guard != 4000000000LL) {
		return 4;
	}
	if (runtime_multi.first != 24) {
		return 5;
	}
	if (runtime_multi.second != 25) {
		return 6;
	}
	if (runtime_multi.tag != 26) {
		return 7;
	}

	Leaf runtime_leaf{27, 28, 29, 30};
	if (runtime_leaf.first != 27) {
		return 8;
	}
	if (runtime_leaf.second != 28) {
		return 9;
	}
	if (runtime_leaf.middle != 29) {
		return 10;
	}
	if (runtime_leaf.last != 30) {
		return 11;
	}

	RecordDerived runtime_record{{{31, 32}, {33, 34}}, 35};
	if (runtime_record.point.x != 31) {
		return 12;
	}
	if (runtime_record.point.y != 32) {
		return 13;
	}
	if (runtime_record.values[0] != 33) {
		return 14;
	}
	if (runtime_record.values[1] != 34) {
		return 15;
	}
	if (runtime_record.tail != 35) {
		return 16;
	}

	Derived runtime_values[2] = {{{36, 37}, 38}, {{39, 40}, 41}};
	if (runtime_values[0].first != 36) {
		return 17;
	}
	if (runtime_values[1].second != 40) {
		return 18;
	}
	if (runtime_values[1].tail != 41) {
		return 19;
	}

	return 42;
}
