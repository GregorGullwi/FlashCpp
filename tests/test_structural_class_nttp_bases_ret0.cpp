struct FirstBase {
	int value;

	constexpr FirstBase(int input)
		: value(input) {}
};

struct SecondBase {
	int value;

	constexpr SecondBase(int input)
		: value(input) {}
};

struct Combined : FirstBase, SecondBase {
	constexpr Combined(int first, int second)
		: FirstBase(first), SecondBase(second) {}
};

template <Combined K>
struct tag {
	static constexpr int value = 0;
};

constexpr Combined first{1, 2};
constexpr Combined second{9, 2};

template <>
struct tag<first> {
	static constexpr int value = 1;
};

int main() {
	return tag<second>::value;
}
