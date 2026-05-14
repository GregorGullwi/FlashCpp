template<typename T>
struct Base {
	using type = int;
};

template<typename T>
struct Mid : Base<T> {
};

template<typename T>
struct Derived : Mid<T> {
	typename Mid<T>::type value{};

	int run() {
		value = 42;
		return value - 42;
	}
};

int main() {
	Derived<int> d;
	return d.run();
}
