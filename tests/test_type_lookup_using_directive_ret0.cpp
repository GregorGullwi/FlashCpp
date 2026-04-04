namespace A {
struct X {
	int value;
};
}

int main() {
	using namespace A;
	X x{42};
	return x.value - 42;
}
