template<typename T>
struct Base {
	using type = int;
};

template<typename T>
struct Mid : Base<T> {
};

template<typename T>
struct Derived : Mid<T> {
	Mid<T>::type value;
};

int main() {
	Derived<int> d;
	(void)d;
	return 0;
}
