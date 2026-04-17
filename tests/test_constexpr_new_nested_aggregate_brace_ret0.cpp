struct Inner {
	int x;
	int y;
};

struct Outer {
	Inner inner;
	int z;
};

constexpr int compileTimeSum() {
	Outer* p = new Outer{{1, 2}, 3};
	int result = p->inner.x + p->inner.y + p->z;
	delete p;
	return result;
}

int runtimeSum() {
	Outer* p = new Outer{{4, 5}, 6};
	int result = p->inner.x + p->inner.y + p->z;
	delete p;
	return result;
}

static_assert(compileTimeSum() == 6);

int main() {
	return (compileTimeSum() == 6 && runtimeSum() == 15) ? 0 : 1;
}
