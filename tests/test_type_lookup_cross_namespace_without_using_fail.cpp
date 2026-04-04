namespace A {
struct X {
	int value;
};
}

int main() {
	X x{42};
	return x.value;
}
