template<typename T>
struct Base {
	template<typename U>
	using rebind = U;
};

template<typename T>
struct Mid : Base<T> {};

template<typename T>
struct Derived {
	using value_type = typename Mid<T>::template rebind<int>;

	int run() {
		value_type value = 42;
		return value - 42;
	}
};

int main() {
	Derived<char> value;
	return value.run();
}
