enum Small : unsigned char {
	A = 5
};

enum Medium : unsigned short {
	B = 6
};

enum Normal : int {
	C = 7
};

enum Large : long long {
	D = 24
};

template <typename T>
struct underlying_type_impl {
	using type = __underlying_type(T);
};

template <typename T>
struct Box {
	typename underlying_type_impl<T>::type value;
};

template <typename T>
typename underlying_type_impl<T>::type toUnderlying(T value) {
	return value;
}

int main() {
	Box<Small> small;
	small.value = toUnderlying(A);
	Box<Medium> medium;
	medium.value = toUnderlying(B);
	Box<Normal> normal;
	normal.value = toUnderlying(C);
	Box<Large> large;
	large.value = toUnderlying(D);
	return small.value + medium.value + normal.value + large.value;
}
