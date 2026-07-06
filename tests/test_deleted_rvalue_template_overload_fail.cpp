template <class T>
T* addressof(T& value) {
	return __builtin_addressof(value);
}

template <class T>
const T* addressof(const T&&) = delete;

int main() {
	return addressof(5) == nullptr ? 0 : 1;
}
