struct Padding { long long guard; };
struct Variants {
	union { short small; int integer; };
	union { long long wide; unsigned narrow; };
};
struct Derived : Padding, Variants {};
int main() {
	Derived object;
	object.guard = 12345678901LL;
	object.integer = 42;
	object.wide = 9876543210LL;
	Variants* base = &object;
	return base->integer == 42 && base->wide == 9876543210LL && object.guard == 12345678901LL ? 0 : 1;
}
