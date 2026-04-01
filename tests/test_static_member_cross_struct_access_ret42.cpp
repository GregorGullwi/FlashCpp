struct B {
	static constexpr int value = 10;
};

struct A {
	static constexpr int value = 20;

	int get() {
		return B::value;
	}
};

int main() {
	A a;
	return a.get() + 32;
}
