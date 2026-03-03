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

int main() {
	AggregateWithTail v = {{{10}, 20}, {{30}, 40}, 42};
	return (v.nested.leaf.value == 10 &&
		v.nested.extra == 20 &&
		v.follow.leaf.value == 30 &&
		v.follow.extra == 40 &&
		v.tail == 42) ? 0 : 1;
}
