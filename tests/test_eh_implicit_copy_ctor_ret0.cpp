int g_inner_copy_count = 0;

struct Inner {
	int value;

	Inner(int v = 0) : value(v) {}

	Inner(const Inner& other) {
		value = other.value + 1;
		g_inner_copy_count++;
	}
};

struct Outer {
	Inner inner;
	int tag;

	Outer(int v, int t) : inner(v), tag(t) {}
};

int main() {
	Outer outer(5, 20);

	try {
		throw outer;
	} catch (Outer caught) {
		if (g_inner_copy_count != 2)
			return 1;
		if (caught.inner.value != 7)
			return 2;
		return caught.tag == 20 ? 0 : 3;
	}

	return 4;
}