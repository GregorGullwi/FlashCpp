constexpr int helper() {
	return 7;
}

template <typename T>
struct Box {
	static constexpr int helper() {
		return static_cast<int>(sizeof(T)) + 38;
	}

	static constexpr int value = helper();
};

template <typename T>
struct Wrapper {
	static constexpr int helper() {
		return static_cast<int>(sizeof(T)) + 40;
	}

	static constexpr int value = helper();
};

int main() {
	if (Box<int>::value != 42) {
		return 1;
	}
	if (Box<char>::value != 39) {
		return 2;
	}
	if (Wrapper<int>::value != 44) {
		return 3;
	}
	if (Wrapper<char>::value != 41) {
		return 4;
	}
	if (Box<int>::value == helper()) {
		return 5;
	}
	if (helper() != 7) {
		return 6;
	}
	return 0;
}
