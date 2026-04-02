template <typename T>
struct Box {
	static int helper() {
		return static_cast<int>(sizeof(T)) + 38;
	}

	int method(int value) {
		return value;
	}

	static int compute() {
		Box other;
		return other.method(helper());
	}
};

int main() {
	if (Box<int>::compute() != 42) {
		return 1;
	}
	if (Box<char>::compute() != 39) {
		return 2;
	}
	return 42;
}
