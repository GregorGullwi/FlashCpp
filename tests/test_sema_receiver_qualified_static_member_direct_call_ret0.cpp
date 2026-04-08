struct Base {
	static int twice(int x) {
		return x * 2;
	}
};

struct Derived : Base {
	int run() {
		return this->Base::twice(21);
	}
};

int main() {
	Derived d;
	if (d.run() != 42)
		return 1;
	if (d.Base::twice(21) != 42)
		return 2;
	return 0;
}
