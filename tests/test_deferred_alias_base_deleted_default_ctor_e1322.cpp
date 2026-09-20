template<class T> struct Base {
	Base() = delete;
};

template<class T> struct Outer {
	using BaseType = T;
	template<class U> struct Derived : BaseType {};
};

int main() {
	Outer<Base<int>>::Derived<int> value;
}
