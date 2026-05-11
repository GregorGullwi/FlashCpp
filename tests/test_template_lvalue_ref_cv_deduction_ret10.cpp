template<typename T>
struct is_const {
	static constexpr int value = 0;
};

template<typename T>
struct is_const<const T> {
	static constexpr int value = 1;
};

template<typename T>
int plain_ref(T&) {
	return is_const<T>::value;
}

template<typename T>
int const_ref(const T&) {
	return is_const<T>::value;
}

int main() {
	const int c = 0;
	return plain_ref(c) * 10 + const_ref(c);
}
