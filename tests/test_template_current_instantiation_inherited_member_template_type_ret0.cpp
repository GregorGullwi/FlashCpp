template<typename T>
struct Holder {
	int value;
};

template<typename T>
struct Base {
	template<typename U>
	using rebind = Holder<U>;
};

template<typename T>
struct Mid : Base<T> {};

template<typename T>
struct Derived : Mid<T>::template rebind<int> {
	int run() {
		this->value = 42;
		return this->value - 42;
	}
};

int main() {
	Derived<char> value;
	return value.run();
}
