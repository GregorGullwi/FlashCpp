struct NoDflt {
	int v;
	constexpr NoDflt(int a, int b) : v(a + b) {}
};

constexpr int make() {
	NoDflt n;
	return n.v;
}

static_assert(make() == 0);

int main() { return 0; }
