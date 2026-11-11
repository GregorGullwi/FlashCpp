struct Base {
	int foo() const { return 1; }
};

struct Derived : Base {
	using Base::foo;
	int foo(int) const { return 2; }
};

template <typename T>
struct BaseTemplate {
	T value;
	int foo() const { return value; }
};

template <typename T>
struct DerivedTemplate : BaseTemplate<T> {
	using BaseTemplate<T>::foo;
	int foo(long) const { return 3; }
};

int main() {
	Derived value{};
	if (value.foo() != 1) return 1;
	if (value.foo(3) != 2) return 2;

	DerivedTemplate<int> templated{};
	templated.value = 7;
	if (templated.foo() != 7) return 3;
	if (templated.foo(2L) != 3) return 4;
	return 0;
}
