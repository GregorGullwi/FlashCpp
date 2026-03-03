// Aggregate brace elision with following members
struct Leaf {
	int value;
};

struct Mid {
	Leaf leaf;
	int extra;
};

struct AggregateWithTail {
	Mid nested;
	Mid follow;
	int tail;
};

struct ArrayWithTail {
	int arr[3];
	int tail;
};

struct MultiDimArrayWithTail {
	int arr[2][3];
	int tail;
};

// Nested struct inside an array — exercises recursive zero-fill
// when trailing array elements are struct-typed and > 64 bits.
struct Inner {
	int a;
	int b;
	int c;
};

struct StructArrayWithTail {
	Inner items[3];
	int tail;
};

int main() {
	AggregateWithTail v = {{{10}, 20}, {{30}, 40}, 42};
	ArrayWithTail a = {1, 2, 3, 7};
	MultiDimArrayWithTail m = {1, 2, 3, 4, 5, 6, 9};
	ArrayWithTail tooFew = {0, 8};

	// Only first element initialized; items[1] and items[2] must be
	// fully zero-filled (exercises recursive zero-fill for nested structs > 64 bits).
	StructArrayWithTail sa = {{{1, 2, 3}}, 99};

	return (v.nested.leaf.value == 10 &&
		v.nested.extra == 20 &&
		v.follow.leaf.value == 30 &&
		v.follow.extra == 40 &&
		v.tail == 42 &&
		a.arr[0] == 1 &&
		a.arr[1] == 2 &&
		a.arr[2] == 3 &&
		a.tail == 7 &&
		m.arr[1][2] == 6 &&
		m.tail == 9 &&
		tooFew.arr[0] == 0 &&
		tooFew.arr[1] == 8 &&
		tooFew.arr[2] == 0 &&
		tooFew.tail == 0 &&
		sa.items[0].a == 1 &&
		sa.items[0].b == 2 &&
		sa.items[0].c == 3 &&
		sa.items[1].a == 0 &&
		sa.items[1].b == 0 &&
		sa.items[1].c == 0 &&
		sa.items[2].a == 0 &&
		sa.items[2].b == 0 &&
		sa.items[2].c == 0 &&
		sa.tail == 99) ? 0 : 1;
}
