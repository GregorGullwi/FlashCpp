struct V {
	virtual ~V() {}
	int v;
	V(int x = 7) : v(x) {}
};

struct B : virtual V {
	B() : V(23) {}
};

struct D : B {
	D() : V(11), B() {}
};

int main() {
	return D{}.v;
}
