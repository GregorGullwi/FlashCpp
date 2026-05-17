int select_overload(long) {
	return 11;
}

template <typename T>
struct Holder {
	static int value;
};

template <typename T>
int Holder<T>::value = select_overload(T{});

int select_overload(int) {
	return 22;
}

template <typename T>
int read_holder_value() {
	return Holder<T>::value;
}

template <typename T, typename Marker>
int relay_holder_value() {
	return read_holder_value<T>();
}

int main() {
	if (relay_holder_value<int, short>() != 11) {
		return 1;
	}
	if (relay_holder_value<long, int>() != 11) {
		return 2;
	}
	if (Holder<int>::value == 22) {
		return 3;
	}
	if (Holder<long>::value != 11) {
		return 4;
	}
	return 0;
}
