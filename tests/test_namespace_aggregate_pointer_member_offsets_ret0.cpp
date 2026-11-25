// Pointer members at different aggregate offsets, plus a pointer member inside
// a nested record, each need a relocation at the leaf's site rather than at
// the enclosing object's start.

int x = 42;
int y = 7;

struct Two {
	int* first;
	int* second;
	long long tail;
};

struct Inner {
	int* p;
	int tag;
};

struct Outer {
	int tag;
	Inner inner;
};

Two two{&x, &y, 9};
Outer outer{5, {&y, 6}};
Inner inners[2] = {{&x, 3}, {&y, 4}};

int main() {
	if (two.first != &x || two.second != &y) {
		return 1;
	}
	if (*two.first != 42 || *two.second != 7) {
		return 2;
	}
	if (two.tail != 9) {
		return 3;
	}
	*two.second = 8;
	if (y != 8) {
		return 4;
	}
	if (outer.tag != 5 || outer.inner.p != &y || outer.inner.tag != 6) {
		return 5;
	}
	if (*outer.inner.p != 8) {
		return 6;
	}
	*outer.inner.p = 9;
	if (y != 9) {
		return 7;
	}
	if (inners[0].p != &x || inners[0].tag != 3 || inners[1].p != &y || inners[1].tag != 4) {
		return 8;
	}
	*inners[0].p = 44;
	if (x != 44) {
		return 9;
	}
	return 0;
}
