namespace A {
struct X {
	int value;
};
}

int main() {
	using A::X;
	X x{42};
	return x.value - 42;
}
