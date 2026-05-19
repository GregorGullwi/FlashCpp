template <class T, T V>
struct Tag {
	static constexpr int value = 0;
};

template <>
struct Tag<short, static_cast<short>(1)> {
	static constexpr int value = 42;
};

template <class T, T V = 1>
struct Use : Tag<T, V> {
};

template <class T, T V = static_cast<T>(1)>
struct UseCast : Tag<T, V> {
};

template <class T, T V = -1>
struct SignedDefault {
	static constexpr int value = V;
};

template <class T, T V = 255>
struct UnsignedDefault {
	static constexpr int value = V;
};

int main() {
	if (Use<short>::value != 42) {
		return 1;
	}
	if (UseCast<short>::value != 42) {
		return 2;
	}
	if (SignedDefault<signed char>::value != -1) {
		return 3;
	}
	if (UnsignedDefault<unsigned char>::value != 255) {
		return 4;
	}
	return Use<int>::value == 0 ? 0 : 5;
}
