struct Base {
	int read() & { return 17; }
	int read() && { return 29; }
};
struct Derived : Base {};
int main() {
	Derived value;
	if (value.Base::read() != 17) return 1;
	if (Derived{}.Base::read() != 29) return 2;
	return 0;
}
