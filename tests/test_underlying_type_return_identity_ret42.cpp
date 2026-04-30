enum MyEnum : int {
	A = 10,
	B = 20,
	C = 12
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
	int c = toUnderlying(C);
	return a + b + c;
}
