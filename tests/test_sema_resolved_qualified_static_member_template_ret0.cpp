template <typename T>
struct Box {
	static int helper() {
		return static_cast<int>(sizeof(T)) + 38;
	}

	static int value() {
		return helper();
	}
};

int readBox() {
	return Box<int>::value();
}

int main() {
	if (readBox() != 42) {
		return 1;
	}
	if (Box<char>::value() != 39) {
		return 2;
	}
	return 0;
}
