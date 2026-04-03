template <typename T>
struct Box {
	static int helper() {
		return int(sizeof(T)) + 38;
	}

	static int value() {
		try {
			return helper();
		} catch (...) {
			return 0;
		}
	}
};

int main() {
	if (Box<int>::value() != 42) {
		return 1;
	}
	if (Box<long long>::value() != 46) {
		return 2;
	}
	return 0;
}
