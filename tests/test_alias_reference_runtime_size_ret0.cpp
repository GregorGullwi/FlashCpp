template <typename T>
struct AliasHolder {
	using ref = T&;
	using const_ref = const T&;
};

template <typename T>
typename AliasHolder<T>::ref forwardRef(typename AliasHolder<T>::ref value) {
	return value;
}

template <typename T>
typename AliasHolder<T>::const_ref asConst(typename AliasHolder<T>::ref value) {
	return value;
}

int bump(typename AliasHolder<int>::ref value) {
	value += 1;
	return value;
}

int main() {
	int value = 41;
	auto&& ref = forwardRef<int>(value);
	if (&ref != &value) {
		return 1;
	}
	if (bump(ref) != 42) {
		return 2;
	}
	return asConst<int>(ref) == 42 ? 0 : 3;
}
