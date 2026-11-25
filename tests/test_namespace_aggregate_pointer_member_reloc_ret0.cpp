// A namespace-scope aggregate with a pointer-valued member stores the member
// address at the member's own offset. The relocation site used to be the
// aggregate's start, leaving the member null while locals initialized fine.

int x = 42;

struct S {
	int* p;
	int tag;
};

S s{&x, 7};

int main() {
	if (s.p != &x) {
		return 1;
	}
	if (*s.p != 42) {
		return 2;
	}
	if (s.tag != 7) {
		return 3;
	}
	*s.p = 43;
	if (x != 43) {
		return 4;
	}
	return 0;
}
