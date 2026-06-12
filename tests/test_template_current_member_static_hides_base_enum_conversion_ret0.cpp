template <typename T>
struct Base {
	enum Small : unsigned char { value = 3 };

	static int select(int) {
		return 7;
	}
};

template <typename T>
struct Derived : Base<T> {
	static int select(long value) {
		return value == 3 ? static_cast<int>(sizeof(T)) + 40 : 1;
	}

	static int run() {
		return select(Base<T>::value);
	}
};

int main() {
	return Derived<int>::run() == 44 ? 0 : 1;
}
