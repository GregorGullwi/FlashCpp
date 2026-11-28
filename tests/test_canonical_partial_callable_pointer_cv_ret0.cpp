template <class T>
struct Signal {
	static constexpr int value = 0;
};

template <class T>
struct Signal<T (*const *)(T)> {
	static constexpr int value = sizeof(T);
};

int main() {
	if (Signal<int (**)(int)>::value != 0) {
		return 1;
	}
	if (Signal<int (*const *)(int)>::value != sizeof(int)) {
		return 2;
	}
	if (Signal<int (**const)(int)>::value != 0 || Signal<int (*volatile *)(int)>::value != 0) {
		return 3;
	}
	if (Signal<int (*const *volatile)(int)>::value != 0) {
		return 4;
	}
	return 0;
}
