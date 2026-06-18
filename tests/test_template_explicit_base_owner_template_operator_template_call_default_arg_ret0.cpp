template <class T>
struct Base {
	template <class U>
	int operator()(U value = U{1}) {
		return static_cast<int>(value);
	}
};

template <class T>
struct Holder : Base<T> {
	template <class U>
	int operator()(U value) {
		return static_cast<int>(value) + 100;
	}

	int run() {
		return this->Base<T>::template operator()<int>() - 1;
	}
};

int main() {
	return Holder<long>{}.run();
}
