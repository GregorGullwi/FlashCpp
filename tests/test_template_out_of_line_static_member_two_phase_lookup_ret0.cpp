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

int main() {
	// Returns 1 if wrong overload selected (post-definition int overload picked instead of long).
	if (Holder<int>::value == 22) {
		return 1;
	}
	// Returns 2 if the initializer was not parsed/applied at all (default zero-initialization),
	// which is a distinct failure mode from picking the wrong overload above.
	if (Holder<int>::value == 0) {
		return 2;
	}
	if (Holder<int>::value != 11) {
		return 3;
	}
	if (Holder<long>::value != 11) {
		return 4;
	}
	return 0;
}
