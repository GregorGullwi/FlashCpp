// A brace-elided base that does not start at offset zero must still receive
// every clause its aggregate leaves need at its own layout offset. A flat
// mapping would put the trailing base's members over the leading base.

struct Leading {
	long long guard;
};

struct Trailing {
	short first;
	short second;
};

struct ElidedDerived : Leading, Trailing {
	int tail;
};

struct BracedDerived : Leading, Trailing {
	int tail;
};

ElidedDerived elided{21LL, 22, 23, 24};
BracedDerived braced{{31LL}, {32, 33}, 34};

int main() {
	if (elided.guard != 21LL) return 1;
	if (elided.first != 22) return 2;
	if (elided.second != 23) return 3;
	if (elided.tail != 24) return 4;
	if (braced.guard != 31LL) return 5;
	if (braced.first != 32) return 6;
	if (braced.second != 33) return 7;
	if (braced.tail != 34) return 8;
	return 0;
}
