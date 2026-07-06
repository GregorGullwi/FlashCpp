namespace ns {
	template <class T>
	T* addressof(T& value) {
		return __builtin_addressof(value);
	}

	template <class T>
	const T* addressof(const T&&) = delete;
}

int main() {
	int value = 5;
	int* ptr = ns::addressof(value);
	return ptr == &value ? 0 : 1;
}
