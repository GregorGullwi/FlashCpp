struct Base {
	virtual ~Base() {}
};

template <typename T>
struct Box {
	static T storage;

	static const T& helperConstRef() {
		return storage;
	}

	static T* helperPtr() {
		return &storage;
	}

	static Base* helperBasePtr() {
		return nullptr;
	}

	static int value() {
		int rebound_const = const_cast<T&>(helperConstRef());
		unsigned long long rebound_ptr = reinterpret_cast<unsigned long long>(helperPtr());
		Base* rebound_base = dynamic_cast<Base*>(helperBasePtr());
		return rebound_const + (rebound_ptr != 0 ? 1 : 0) + (rebound_base != nullptr ? 1 : 0);
	}
};

template <typename T>
T Box<T>::storage = static_cast<T>(sizeof(T) + 40);

int main() {
	if (Box<char>::value() != 42) {
		return 1;
	}
	if (Box<int>::value() != 45) {
		return 2;
	}
	return 0;
}
