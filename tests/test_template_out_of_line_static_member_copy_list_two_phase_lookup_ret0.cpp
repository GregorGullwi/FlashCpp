int select_overload(long) {
	return 31;
}

template <typename T>
struct Holder {
	static int value;
};

template <typename T>
int Holder<T>::value = {select_overload(T{})};

int select_overload(int) {
	return 62;
}

int main() {
	if (Holder<int>::value == 0) {
		return 1;
	}
	if (Holder<int>::value != 31) {
		return 2;
	}
	if (Holder<long>::value != 31) {
		return 3;
	}
	return 0;
}
