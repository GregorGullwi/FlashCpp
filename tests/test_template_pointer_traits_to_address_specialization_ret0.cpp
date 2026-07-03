template <class T>
struct pointer_traits;

template <class T>
concept HasToAddress = requires(const T& value) {
	pointer_traits<T>::to_address(value);
};

template <class T>
T* local_to_address(T* value) {
	return value;
}

template <class Ptr>
auto local_to_address(const Ptr& value) {
	if constexpr (HasToAddress<Ptr>) {
		return pointer_traits<Ptr>::to_address(value);
	} else {
		return local_to_address(value.operator->());
	}
}

template <class T>
struct Iterator {
	T* ptr;
};

template <class T>
struct pointer_traits<Iterator<T>> {
	static T* to_address(Iterator<T> value) {
		return value.ptr;
	}
};

int main() {
	int value = 7;
	Iterator<int> it{&value};
	int* ptr = local_to_address(it);
	return *ptr == 7 ? 0 : 1;
}
