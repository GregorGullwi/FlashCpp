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
	return toUnderlying(A) + toUnderlying(B) + toUnderlying(C);
}
