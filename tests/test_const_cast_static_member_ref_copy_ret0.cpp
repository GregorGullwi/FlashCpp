template <typename T>
struct RefBox {
	static T storage;

	static const T& helper() {
		return storage;
	}

	static int value() {
		int rebound = const_cast<T&>(helper());
		return rebound;
	}
};

template <typename T>
T RefBox<T>::storage = static_cast<T>(sizeof(T) + 40);

int main() {
	if (RefBox<char>::value() != 41) {
		return 1;
	}
	if (RefBox<int>::value() != 44) {
		return 2;
	}
	return 0;
}
