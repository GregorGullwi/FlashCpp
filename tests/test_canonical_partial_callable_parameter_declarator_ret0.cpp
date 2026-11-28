template <class T>
struct Signal {
	static constexpr int value = 0;
};

template <class T>
struct Signal<T (**)(T*)> {
	static constexpr int value = sizeof(T);
};

int main() {
	if (Signal<int (**)(int)>::value != 0) {
		return 1;
	}
	if (Signal<int (**)(int*)>::value != sizeof(int)) {
		return 2;
	}
	if (Signal<short* (**)(short**)>::value != sizeof(short*)) {
		return 3;
	}
	if (Signal<long (**)(long* const)>::value != sizeof(long)) {
		return 4;
	}
	if (Signal<int (**)(const int*)>::value != 0) {
		return 5;
	}
	if (Signal<char (**)(char[3])>::value != sizeof(char)) {
		return 6;
	}
	return 0;
}
