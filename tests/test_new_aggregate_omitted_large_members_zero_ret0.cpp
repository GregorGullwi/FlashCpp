struct Inner {
	int a;
	int b;
	int c;
};

struct Outer {
	int z;
	Inner inner;
	int values[3];
};

int main() {
	Outer* p = new Outer{7};
	int sum = p->z + p->inner.a + p->inner.b + p->inner.c + p->values[0] + p->values[1] + p->values[2];
	delete p;
	return sum == 7 ? 0 : 1;
}
