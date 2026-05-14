enum First {
	FirstValue = 7
};

enum Second {
	SecondValue = 7
};

template <auto V>
struct tag {
	static constexpr int value = 9;
};

template <>
struct tag<FirstValue> {
	static constexpr int value = 1;
};

template <>
struct tag<SecondValue> {
	static constexpr int value = 2;
};

int main() {
	if (tag<FirstValue>::value != 1) return 1;
	if (tag<SecondValue>::value != 2) return 2;
	return 0;
}
