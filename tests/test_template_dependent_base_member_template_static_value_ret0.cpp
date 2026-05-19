template <typename T>
struct Base {
	template <typename U>
	struct Inner {
		static constexpr int value = sizeof(T) + sizeof(U);
	};
};

template <typename T>
struct Derived : Base<T> {
	static constexpr int value = Derived<T>::template Inner<int>::value;
};

int main() {
	return Derived<char>::value == 5 ? 0 : 1;
}
