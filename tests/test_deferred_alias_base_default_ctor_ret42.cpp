template<class T> struct Base {
	int value;
	Base() : value(42) {}
};

template<class T> struct Outer {
	using BaseType = T;
	template<class U> struct Derived : BaseType {};
};

int main() {
	Outer<Base<int>>::Derived<int> default_initialized;
	Outer<Base<int>>::Derived<int> value_initialized{};
	return default_initialized.value + value_initialized.value - 42;
}
