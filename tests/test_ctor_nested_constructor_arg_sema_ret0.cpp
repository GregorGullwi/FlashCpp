struct Inner {
	int value;
	Inner(int v) : value(v) {}
};

using AliasInner = Inner;

struct Sink {
	int value;
	Sink(Inner inner) : value(inner.value) {}
};

int main() {
	Sink direct((Inner(11)));
	if (direct.value != 11)
		return 1;

	Sink alias((AliasInner(31)));
	if (alias.value != 31)
		return 2;

	return 0;
}
