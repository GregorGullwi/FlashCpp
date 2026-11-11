struct A {
	int value;
	int foo() const { return value; }
	static int static_value;
};
struct B : virtual A {};
struct C : B {};
struct D : C {
	using A::value;
	using A::foo;
	using A::static_value;
	int foo(int) const { return 2; }
};
int A::static_value = 9;
int main() {
	D object{};
	object.value = 7;
	if (object.foo() != 7) return 1;
	if (object.foo(1) != 2) return 2;
	if (D::static_value != 9) return 3;
	return 0;
}
