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

int main() {
	AggregateWithTail v = {{{10}, 20}, {{30}, 40}, 42};
	ArrayWithTail a = {1, 2, 3, 7};

	return (v.nested.leaf.value == 10 &&
		v.nested.extra == 20 &&
		v.follow.leaf.value == 30 &&
		v.follow.extra == 40 &&
		v.tail == 42 &&
		a.arr[0] == 1 &&
		a.arr[1] == 2 &&
		a.arr[2] == 3 &&
		a.tail == 7) ? 0 : 1;
}
