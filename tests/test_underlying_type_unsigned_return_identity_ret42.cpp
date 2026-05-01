enum Small : unsigned char {
	A = 20,
	B = 22
};

template <typename T>
struct underlying_type_impl {
	using type = __underlying_type(T);
};

template <typename T>
typename underlying_type_impl<T>::type toUnderlying(T value) {
	return value;
}

int main() {
	int a = toUnderlying(A);
	int b = toUnderlying(B);
	return a + b;
}
